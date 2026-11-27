// SPDX-License-Identifier: GPL-3.0-or-later
#include "LayoutStore.h"

#include <Logging.h>
namespace booksync::storage {
bool LayoutStore::validateFile(FsFile& file, void* context) {
  auto& self = *static_cast<LayoutStore*>(context);
  // The HAL's size_t accessor can wrap above 4 GiB on ESP32. Check the full
  // size before narrowing; native profiles already have a 64-bit size_t.
#if SIZE_MAX < UINT64_MAX
  const auto bytes = file.fileSize64();
#else
  const auto bytes = file.fileSize();
#endif
  if (!bytes || bytes > Layout::MaxBytes) {
    LOG_ERR("BSL", "Layout file exceeds byte budget or is empty");
    return false;
  }
  LayoutInput input{[](void* p, size_t offset, void* out, size_t n) {
                      auto& f = *static_cast<FsFile*>(p);
                      return f.seekSet(offset) && f.read(out, n) == static_cast<int>(n);
                    },
                    &file, static_cast<size_t>(bytes)};
  return self.validator_.validate(input, self.device_, self.spans_, self.context_);
}
LayoutSource LayoutStore::load() {
  selected_ = nullptr;
  switch (FilePublish::select(paths_, validateFile, this)) {
    case PublishedFile::Current:
      selected_ = paths_.current;
      return LayoutSource::Current;
    case PublishedFile::Backup:
      selected_ = paths_.backup;
      return LayoutSource::Backup;
    default:
      LOG_INF("BSL", "Using default Home layout; saved bytes retained");
      return LayoutSource::Default;
  }
}
bool LayoutStore::save(LayoutInput input) {
  if (!validator_.validate(input, device_, spans_, context_)) return false;
  // Do not overwrite even a damaged primary on an explicit save. Recovery is
  // read-only; a future recovery flow can preserve damaged bytes before repair.
  if (Storage.exists(paths_.current)) {
    FsFile old = Storage.open(paths_.current, O_RDONLY);
    if (!old) {
      LOG_ERR("BSL", "Layout open failed");
      return false;
    }
    const bool valid = validateFile(old, this);
    const bool closed = old.close();
    if (!valid || !closed) {
      LOG_ERR("BSL", "Layout source invalid or close failed; bytes retained");
      return false;
    }
  }
  // A failed publication can have renamed either candidate: require load() to
  // reconcile before exposing another selected path.
  selected_ = nullptr;
  FilePublish writer(paths_, validateFile, this);
  if (!writer.begin(input.bytes)) return false;
  for (size_t offset = 0; offset < input.bytes;) {
    const size_t n = input.bytes - offset < sizeof(copy_) ? input.bytes - offset : sizeof(copy_);
    if (!input.read(input.context, offset, copy_, n)) {
      LOG_ERR("BSL", "Layout source read failed");
      return false;
    }
    if (!writer.write(copy_, n)) return false;
    offset += n;
  }
  if (!writer.finish()) return false;
  selected_ = paths_.current;
  return true;
}
}  // namespace booksync::storage
