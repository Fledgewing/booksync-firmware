// SPDX-License-Identifier: GPL-3.0-or-later
#include "DownloadJobs.h"

#include <Logging.h>

#include <algorithm>
#include <cstring>
#include <limits>
#if defined(ARDUINO) && !defined(SIMULATOR)
#include <mbedtls/sha256.h>
#else
#include <openssl/sha.h>
#endif
namespace booksync {
namespace {
constexpr std::array<uint8_t, 8> Magic{'B', 'S', 'D', 'L', 3, 0, 0, 0};
uint32_t checksum(const void* data, size_t n, uint32_t hash = 2166136261u) {
  const auto* p = static_cast<const uint8_t*>(data);
  while (n--) hash = (hash ^ *p++) * 16777619u;
  return hash;
}
template <size_t N>
bool text(const std::array<char, N>& s, bool required = true) {
  return (!required || s[0]) && std::memchr(s.data(), 0, N);
}
template <size_t N>
bool nonzero(const std::array<uint8_t, N>& value) {
  for (auto byte : value)
    if (byte) return true;
  return false;
}
bool sameSelection(const DownloadSpec& a, const DownloadSpec& b) {
  return a.identity.book == b.identity.book && a.identity.edition == b.identity.edition &&
         (!nonzero(a.identity.revision.bytes) || !nonzero(b.identity.revision.bytes) ||
          a.identity.revision == b.identity.revision) &&
         a.proof == b.proof && a.version == b.version && a.server == b.server && a.account == b.account &&
         a.book == b.book && a.file == b.file && a.variant == b.variant;
}
bool scopeMatches(const DownloadSpec& s, storage::ScopeView scope) {
  return scope.server == s.server.data() && scope.account == s.account.data();
}
// One completion log, with 130 bytes of bounded stack scratch and no heap.
void logReplacement(const DownloadSpec& spec, const storage::Revision& previous) {
  static constexpr char Hex[] = "0123456789abcdef";
  char oldHash[65], newHash[65];
  for (size_t i = 0; i < previous.bytes.size(); ++i) {
    oldHash[i * 2] = Hex[previous.bytes[i] >> 4];
    oldHash[i * 2 + 1] = Hex[previous.bytes[i] & 15];
    newHash[i * 2] = Hex[spec.identity.revision.bytes[i] >> 4];
    newHash[i * 2 + 1] = Hex[spec.identity.revision.bytes[i] & 15];
  }
  oldHash[64] = newHash[64] = 0;
  LOG_INF("BSC", "Copy replaced: %s (%s -> %s)", spec.path.data(), oldHash, newHash);
}
// The hash state is under 256 bytes; the shared chunk workspace belongs to the
// worker. Every read is bounded and checks admission, including final validation.
struct Hash {
#if defined(ARDUINO) && !defined(SIMULATOR)
  mbedtls_sha256_context state;
  Hash() { mbedtls_sha256_init(&state); }
  ~Hash() { mbedtls_sha256_free(&state); }
  bool begin() { return mbedtls_sha256_starts(&state, 0) == 0; }
  bool add(const uint8_t* p, size_t n) { return mbedtls_sha256_update(&state, p, n) == 0; }
  bool finish(uint8_t* p) { return mbedtls_sha256_finish(&state, p) == 0; }
#else
  SHA256_CTX state{};
  bool begin() { return SHA256_Init(&state) == 1; }
  bool add(const uint8_t* p, size_t n) { return SHA256_Update(&state, p, n) == 1; }
  bool finish(uint8_t* p) { return SHA256_Final(p, &state) == 1; }
#endif
};
}  // namespace
DownloadJobs::DownloadJobs(storage::PublishPaths journal, Admission& admission, DownloadPorts ports)
    : journal_(journal),
      admission_(admission),
      ports_(ports),
      artifact_({record_.spec.path.data(), staging_.data(), backup_.data()}, artifactValid, this) {}
DownloadJobs::~DownloadJobs() {
  if (running_) settle(DownloadState::Paused);
  if (draining_ && !admission_.finish()) LOG_ERR("BSD", "Download owner destroyed before network drain");
}
bool DownloadJobs::same(DeliveryTicket current) const {
  return scoped_ && current.owner == owner_.owner && current.epoch == owner_.epoch;
}
bool DownloadJobs::replacementClosed() const {
  return !nonzero(record_.previous.bytes) ||
         (ports_.replacementClosed && ports_.replacementClosed(ports_.context, record_.spec.path.data()));
}
bool DownloadJobs::permitted() const { return admission_.permitted() && replacementClosed(); }
bool DownloadJobs::canRecover() const { return admission_.storageIdle() && replacementClosed(); }
bool DownloadJobs::recordValid(FsFile& file, void*) {
  const auto length = file.fileSize();
  if (length != JournalBytes && length != JournalBytes - 32) return false;
  // Streaming validation avoids a second record on the task stack. Explicit
  // version/size plus checksum rejects torn and unknown records; retain bytes.
  uint8_t bytes[128];
  uint32_t hash = 2166136261u;
  size_t remaining = length - 4;
  bool first = true;
  while (remaining) {
    const auto n = std::min(remaining, sizeof(bytes));
    if (file.read(bytes, n) != int(n)) return false;
    if (first) {
      const auto version = length == JournalBytes ? 3 : 2;
      for (size_t i = 0; i < Magic.size(); ++i)
        if (bytes[i] != (i == 4 ? version : Magic[i])) return false;
    }
    first = false;
    hash = checksum(bytes, n, hash);
    remaining -= n;
  }
  uint8_t stored[4];
  if (file.read(stored, 4) != 4) return false;
  uint32_t expected = 0;
  for (unsigned i = 0; i < 4; ++i) expected |= uint32_t(stored[i]) << (8 * i);
  return hash == expected;
}
bool DownloadJobs::codec(bool encode) {
  size_t offset = 0;
  auto bytes = [&](void* data, size_t n) {
    if (encode)
      std::memcpy(buffer_.data() + offset, data, n);
    else
      std::memcpy(data, buffer_.data() + offset, n);
    offset += n;
  };
  auto number = [&](uint64_t& value) {
    if (encode)
      for (unsigned i = 0; i < 8; ++i) buffer_[offset + i] = uint8_t(value >> (8 * i));
    else {
      value = 0;
      for (unsigned i = 0; i < 8; ++i) value |= uint64_t(buffer_[offset + i]) << (8 * i);
    }
    offset += 8;
  };
  if (encode) std::memcpy(buffer_.data(), Magic.data(), Magic.size());
  offset = Magic.size();
  number(record_.id);
  bytes(&record_.canceled, 1);
  auto& s = record_.spec;
  bytes(s.identity.book.bytes.data(), 16);
  bytes(s.identity.edition.bytes.data(), 16);
  bytes(s.identity.revision.bytes.data(), 32);
  bytes(s.server.data(), 64);
  bytes(s.account.data(), 64);
  bytes(s.book.data(), 64);
  bytes(s.file.data(), 64);
  bytes(s.variant.data(), 64);
  bytes(s.version.data(), 64);
  bytes(s.proof.data(), 65);
  bytes(s.path.data(), 240);
  number(s.bytes);
  uint8_t known = s.sizeKnown ? 1 : 0;
  bytes(&known, 1);
  if (known > 1) return false;
  s.sizeKnown = known;
  bytes(record_.validator.data(), 128);
  // V2 first-acquisition journals remain readable; V3 adds old-revision proof.
  const bool legacy = !encode && buffer_[4] == 2;
  if (!legacy) bytes(record_.previous.bytes.data(), 32);
  const auto length = legacy ? JournalBytes - 32 : JournalBytes;
  if (offset != length - 4) return false;
  if (encode) {
    const auto hash = checksum(buffer_.data(), offset);
    for (unsigned i = 0; i < 4; ++i) buffer_[offset + i] = uint8_t(hash >> (8 * i));
  }
  return true;
}
bool DownloadJobs::save() {
  if (!codec(true)) return false;
  storage::FilePublish publish(journal_, recordValid);
  return publish.begin(JournalBytes) && publish.write(buffer_.data(), JournalBytes) && publish.finish();
}
bool DownloadJobs::accepts(const DownloadSpec& s) const {
  if (!text(s.server) || !text(s.account) || !text(s.book) || !text(s.file) || !text(s.variant, false) ||
      !text(s.path) || !text(s.version, false) || !text(s.proof, false) || !nonzero(s.identity.book.bytes) ||
      !nonzero(s.identity.edition.bytes) || (!nonzero(s.identity.revision.bytes) && !s.proof[0]) ||
      (s.sizeKnown && (!s.bytes || s.bytes > MaxBytes)))
    return false;
  const char* p = s.path.data();
  const size_t n = std::strlen(p);
  if (p[0] != '/' || n < 2 || n + 4 >= staging_.size() || p[n - 1] == '/' || std::strstr(p, "//") ||
      std::strstr(p, "/../") || std::strstr(p, "/./") || std::strchr(p, '\\'))
    return false;
  const auto* last = std::strrchr(p, '/') + 1;
  if (!std::strcmp(last, ".") || !std::strcmp(last, "..")) return false;
  // Reserve only an EPUB destination and its exact staging siblings. F24 owns
  // replacement; this worker refuses unrelated existing files.
  if (n < 5) return false;
  const auto* extension = p + n - 5;
  if (extension[0] != '.' || (extension[1] != 'e' && extension[1] != 'E') ||
      (extension[2] != 'p' && extension[2] != 'P') || (extension[3] != 'u' && extension[3] != 'U') ||
      (extension[4] != 'b' && extension[4] != 'B'))
    return false;
  if (!std::strcmp(p, journal_.current) || !std::strcmp(p, journal_.staging) || !std::strcmp(p, journal_.backup))
    return false;
  return true;
}
bool DownloadJobs::setPaths(const DownloadSpec& s) {
  if (!accepts(s)) return false;
  const char* p = s.path.data();
  const size_t n = std::strlen(p);
  std::memcpy(staging_.data(), p, n);
  std::memcpy(staging_.data() + n, ".tmp", 5);
  std::memcpy(backup_.data(), p, n);
  std::memcpy(backup_.data() + n, ".bak", 5);
  return true;
}
bool DownloadJobs::selectJournal(storage::PublishPaths paths, storage::ScopeView scope, DeliveryTicket current) {
  if (busy() || artifactOpen_ || !paths.current || !paths.staging || !paths.backup) return false;
  journal_ = paths;
  return recover(scope, current);
}
bool DownloadJobs::matchesSelection(const DownloadSpec& spec, DeliveryTicket current) const {
  return same(current) && status_.id && sameSelection(record_.spec, spec);
}
bool DownloadJobs::recover(storage::ScopeView scope, DeliveryTicket current) {
  if (busy() || !current.owner || !current.epoch || scope.server.empty() || scope.account.empty() ||
      scope.server.find('\0') != scope.server.npos || scope.account.find('\0') != scope.account.npos ||
      !ports_.freeBytes || !ports_.link)
    return false;
  scoped_ = false;
  status_ = {};
  record_ = {};
  owner_ = current;
  const auto selected = storage::FilePublish::select(journal_, recordValid);
  if (selected == storage::PublishedFile::Unreadable) {
    LOG_ERR("BSD", "Unreadable download journal retained");
    return false;
  }
  if (selected != storage::PublishedFile::Missing) {
    FsFile file =
        Storage.open(selected == storage::PublishedFile::Current ? journal_.current : journal_.backup, O_RDONLY);
    if (!file) {
      LOG_ERR("BSD", "Download journal open failed");
      return false;
    }
    const auto length = file.fileSize();
    const bool read = length <= buffer_.size() && file.read(buffer_.data(), length) == static_cast<int>(length);
    const bool closed = file.close();
    if (!read || !closed || !codec(false) || !setPaths(record_.spec) || !text(record_.validator, false) ||
        !record_.id || record_.canceled > 1 || !scopeMatches(record_.spec, scope)) {
      LOG_ERR("BSD", "Download journal scope/record rejected");
      return false;
    }
    status_.id = record_.id;
    status_.state = record_.canceled ? DownloadState::Canceled : DownloadState::Paused;
    status_.total = record_.spec.bytes;
    status_.totalKnown = record_.spec.sizeKnown;
    if (Storage.exists(staging_.data())) {
      FsFile part = Storage.open(staging_.data(), O_RDONLY);
      if (!part) {
        LOG_ERR("BSD", "Download staging open failed");
        return false;
      }
      status_.received = part.fileSize();
      if (!part.close() || status_.received > MaxBytes) return false;
    }
  } else {
    if (scope.server.size() >= record_.spec.server.size() || scope.account.size() >= record_.spec.account.size())
      return false;
    std::memcpy(record_.spec.server.data(), scope.server.data(), scope.server.size());
    std::memcpy(record_.spec.account.data(), scope.account.data(), scope.account.size());
  }
  scoped_ = true;
  return true;
}
bool DownloadJobs::admit() {
  if (busy()) return false;
  const auto result = admission_.begin(TAILNET_JOB_CATALOG);
  if (result != AdmissionResult::Admitted) {
    draining_ = result != AdmissionResult::Busy && admission_.busy();
    return false;
  }
  tls_ = admission_.acquire(Resource::Tls);
  worker_ = admission_.acquire(Resource::Worker);
  catalog_ = admission_.acquire(Resource::Catalog);
  running_ = true;
  if (!tls_ || !worker_ || !catalog_) {
    fail(DownloadError::Busy);
    return false;
  }
  return true;
}
void DownloadJobs::settle(DownloadState state, DownloadError error) {
  if (source_) source_->close();
  source_ = nullptr;
  proofSource_ = nullptr;
  if (artifactOpen_) {
    if (!artifact_.checkpoint()) LOG_ERR("BSD", "Partial download checkpoint failed");
    artifactOpen_ = false;
  }
  worker_.reset();
  tls_.reset();
  catalog_.reset();
  running_ = false;
  status_.state = state;
  status_.error = error;
  ++status_.sequence;
  draining_ = !admission_.finish();
}
void DownloadJobs::fail(DownloadError error) {
  LOG_ERR("BSD", "%s stopped: %u", nonzero(record_.previous.bytes) ? "Copy update" : "Download", unsigned(error));
  settle(DownloadState::Failed, error);
}
bool DownloadJobs::artifactValid(FsFile& file, void* context) {
  return static_cast<DownloadJobs*>(context)->verify(file, true);
}
bool DownloadJobs::recoveryValid(FsFile& file, void* context) {
  return static_cast<DownloadJobs*>(context)->verify(file, true, true);
}
bool DownloadJobs::verify(FsFile& file, bool allowPrevious, bool offline) {
  const bool previousAllowed = allowPrevious && nonzero(record_.previous.bytes);
  if (file.fileSize() > MaxBytes ||
      (!previousAllowed && record_.spec.sizeKnown && file.fileSize() != record_.spec.bytes))
    return false;
  Hash hash;
  if (!hash.begin()) return false;
  uint64_t remaining = file.fileSize();
  if (!remaining) return false;
  while (remaining) {
    if (offline ? !canRecover() : !permitted()) return false;
    const auto n = static_cast<size_t>(std::min<uint64_t>(remaining, buffer_.size()));
    if (file.read(buffer_.data(), n) != int(n) || !hash.add(buffer_.data(), n)) return false;
    remaining -= n;
  }
  if (!hash.finish(computed_.bytes.data()) || (offline && !canRecover())) return false;
  if (previousAllowed && computed_ == record_.previous) return true;
  if (record_.spec.sizeKnown && file.fileSize() != record_.spec.bytes) return false;
  if (nonzero(record_.spec.identity.revision.bytes)) return computed_ == record_.spec.identity.revision;
  if (offline) return false;  // Boot recovery never asks a transport/verifier for proof.
  DownloadCheck check{buffer_.data(), buffer_.size(),
                      [](void* p) { return static_cast<DownloadJobs*>(p)->permitted(); }, this};
  return proofSource_ && proofSource_->verify(file, record_.spec, check) && permitted();
}
bool DownloadJobs::verifyPath(const char* path) {
  if (!permitted()) return false;
  FsFile file = Storage.open(path, O_RDONLY);
  if (!file) {
    LOG_ERR("BSD", "Download candidate open failed");
    return false;
  }
  const bool valid = verify(file);
  const bool closed = file.close();
  return valid && closed;
}
uint64_t DownloadJobs::start(const DownloadSpec& spec, DownloadSource& source, DeliveryTicket current) {
  return startImpl(spec, {}, source, current);
}
uint64_t DownloadJobs::startImpl(const DownloadSpec& spec, storage::Revision previous, DownloadSource& source,
                                 DeliveryTicket current) {
  if (!same(current)) return 0;
  if (busy()) return sameSelection(record_.spec, spec) ? status_.id : 0;
  // Never replace a paused run implicitly. Resume must name that run.
  if (status_.id && sameSelection(record_.spec, spec) && status_.state != DownloadState::Canceled) return status_.id;
  if (status_.id && status_.state != DownloadState::Complete && status_.state != DownloadState::Canceled) return 0;
  if (!accepts(spec) || spec.server != record_.spec.server || spec.account != record_.spec.account ||
      record_.id == std::numeric_limits<uint64_t>::max()) {
    LOG_ERR("BSD", "Invalid download selection rejected");
    return 0;
  }
  if (nonzero(previous.bytes) &&
      (!ports_.replacementClosed || !ports_.replacementClosed(ports_.context, spec.path.data()))) {
    LOG_INF("BSC", "Copy deferred: target lifetime unavailable/open");
    return 0;
  }
  if (!admit()) return 0;
  setPaths(spec);
  // A rejected request never claims another file's staging or poisons the last
  // job. Keep its identity and allow a different resolved destination to retry.
  const bool ownedBackup = nonzero(previous.bytes) && nonzero(record_.previous.bytes) &&
                           record_.spec.path == spec.path && record_.spec.server == spec.server &&
                           record_.spec.account == spec.account && record_.spec.identity.book == spec.identity.book &&
                           record_.spec.identity.edition == spec.identity.edition &&
                           record_.spec.identity.revision == previous;
  // Only the last journaled transaction may retire its retained backup. Verify
  // both files before accepting another revision; unrelated siblings survive.
  bool backupSafe = !Storage.exists(backup_.data());
  if (!backupSafe && ownedBackup && verifyPath(spec.path.data())) {
    FsFile file = Storage.open(backup_.data(), O_RDONLY);
    if (file) {
      backupSafe = verify(file, true) && computed_ == record_.previous;
      if (!file.close()) backupSafe = false;
    }
  }
  if (Storage.exists(staging_.data()) || !backupSafe) {
    LOG_ERR("BSD", "Unowned download staging/backup retained");
    setPaths(record_.spec);
    settle(status_.state);
    return 0;
  }
  const auto id = record_.id + 1;
  record_ = {};
  record_.id = id;
  record_.spec = spec;
  record_.previous = previous;
  status_ = {};
  status_.id = id;
  if (!save()) {
    fail(DownloadError::StorageFailure);
    return id;
  }
  begin(source);
  return id;
}
bool DownloadJobs::resume(uint64_t id, DownloadSource& source, DeliveryTicket current) {
  if (!same(current) || !id || id != status_.id || busy() || record_.canceled ||
      status_.state == DownloadState::Complete || !replacementClosed())
    return false;
  if (!admit()) return false;
  return begin(source);
}
bool DownloadJobs::begin(DownloadSource& source) {
  status_.error = DownloadError::None;
  proofSource_ = &source;
  if (!permitted()) {
    settle(DownloadState::Paused);
    return false;
  }
  if (!restorePrevious()) {
    fail(DownloadError::StorageFailure);
    return false;
  }
  // A replacement only owns bytes matching its journaled old revision. Existing
  // unrelated files still fail closed, including an externally changed SD file.
  if (Storage.exists(record_.spec.path.data())) {
    const bool current = nonzero(record_.spec.identity.revision.bytes) && verifyPath(record_.spec.path.data());
    if (current) {
      storage::FilePublish published({record_.spec.path.data(), staging_.data(), backup_.data()}, artifactValid, this);
      if (!published.confirm()) {
        fail(DownloadError::StorageFailure);
        return false;
      }
      status_.state = DownloadState::Linking;
      ++status_.sequence;
      return true;
    }
    if (!nonzero(record_.previous.bytes) ||
        !storage::FilePublish::validateCurrent({record_.spec.path.data(), staging_.data(), backup_.data()},
                                               artifactValid, this) ||
        computed_ != record_.previous) {
      fail(DownloadError::Digest);
      return false;
    }
  } else if (nonzero(record_.previous.bytes)) {
    fail(DownloadError::StorageFailure);
    return false;
  }
  status_.received = 0;
  if (Storage.exists(staging_.data()) && record_.validator[0]) {
    FsFile part = Storage.open(staging_.data(), O_RDONLY);
    if (!part) {
      fail(DownloadError::StorageFailure);
      return false;
    }
    status_.received = part.fileSize();
    if (!part.close() || status_.received > MaxBytes) {
      fail(DownloadError::StorageFailure);
      return false;
    }
    // A full but unverified/corrupt staging file must restart on explicit Resume.
    if (nonzero(record_.spec.identity.revision.bytes) && record_.spec.sizeKnown &&
        status_.received == record_.spec.bytes && !verifyPath(staging_.data()))
      status_.received = 0;
  }
  source_ = &source;
  reply_ = {};
  if (!source.open(record_.spec, status_.received, record_.validator.data(), reply_)) {
    fail(source.error());
    return false;
  }
  if (!permitted()) {
    settle(DownloadState::Paused);
    return false;
  }
  if (reply_.identity != record_.spec.identity) {
    fail(DownloadError::Edition);
    return false;
  }
  if (!text(reply_.validator, false) ||
      (reply_.offset != 0 &&
       (reply_.offset != status_.received || !record_.validator[0] || reply_.validator != record_.validator)) ||
      (reply_.sizeKnown && (!reply_.bytes || reply_.bytes > MaxBytes)) ||
      (record_.spec.sizeKnown && reply_.sizeKnown && reply_.bytes != record_.spec.bytes)) {
    fail(DownloadError::Invalid);
    return false;
  }
  status_.received = reply_.offset;
  status_.totalKnown = record_.spec.sizeKnown || reply_.sizeKnown;
  status_.total = record_.spec.sizeKnown ? record_.spec.bytes : (reply_.sizeKnown ? reply_.bytes : 0);
  const auto remaining = status_.totalKnown
                             ? (status_.received <= status_.total ? status_.total - status_.received : MaxBytes + 1)
                             : Chunk;
  const auto free = ports_.freeBytes(ports_.context);
  if (remaining > MaxBytes || free < Headroom || free - Headroom < remaining) {
    fail(DownloadError::NoSpace);
    return false;
  }
  record_.validator = reply_.validator;
  if (!record_.spec.sizeKnown && reply_.sizeKnown) {
    record_.spec.bytes = reply_.bytes;
    record_.spec.sizeKnown = true;
  }
  if (!save()) {
    fail(DownloadError::StorageFailure);
    return false;
  }
  const auto expected = status_.totalKnown ? status_.total : MaxBytes;
  if (!status_.received) {
    // Reuse cold-path scratch; preserve the frozen destination while creating
    // its parent hierarchy under the admitted storage writer.
    const auto parent = std::strrchr(record_.spec.path.data(), '/') - record_.spec.path.data();
    if (parent) {
      std::memcpy(buffer_.data(), record_.spec.path.data(), parent);
      buffer_[parent] = 0;
      if (!Storage.ensureDirectoryExists(reinterpret_cast<const char*>(buffer_.data()))) {
        fail(DownloadError::StorageFailure);
        return false;
      }
    }
  }
  artifactOpen_ = status_.received ? artifact_.resume(expected, status_.received) : artifact_.begin(expected);
  if (!artifactOpen_) {
    fail(DownloadError::StorageFailure);
    return false;
  }
  flushed_ = status_.received;
  status_.state = DownloadState::Transferring;
  ++status_.sequence;
  return true;
}
void DownloadJobs::cancel(uint64_t id, DeliveryTicket current) {
  if (!same(current) || !id || id != status_.id) return;
  if (!replacementClosed()) {
    if (running_) settle(DownloadState::Paused);
    return;
  }
  // Paused/completed jobs still need storage ownership. Refuse at the reading
  // gate or while another service owns the phase; the caller may retry at Home.
  if (!running_ && !admit()) return;
  if (!permitted()) {
    settle(DownloadState::Paused);
    return;
  }
  if (source_) source_->close();
  source_ = nullptr;
  if (artifactOpen_) {
    const bool closed = artifact_.checkpoint();
    artifactOpen_ = false;
    if (!closed) {
      fail(DownloadError::StorageFailure);
      return;
    }
  }
  if (!restorePrevious()) {
    fail(DownloadError::StorageFailure);
    return;
  }
  // Durable revocation first: power loss while removing staging cannot resume it.
  record_.canceled = 1;
  if (!save()) {
    fail(DownloadError::StorageFailure);
    return;
  }
  if (Storage.exists(staging_.data()) && !Storage.remove(staging_.data())) LOG_ERR("BSD", "Canceled staging retained");
  settle(DownloadState::Canceled);
}
void DownloadJobs::tick(DeliveryTicket current) {
  if (draining_) {
    draining_ = !admission_.finish();
    return;
  }
  if (!running_) return;
  if (!same(current) || !permitted()) {
    settle(DownloadState::Paused);
    return;
  }
  if (status_.state == DownloadState::Linking) {
    if (!ports_.link(ports_.context, record_.spec)) {
      settle(DownloadState::Linking, DownloadError::StorageFailure);
      return;
    }
    if (nonzero(record_.previous.bytes)) {
      // Do not advance the single journal to another book with an orphaned
      // rollback file. Retire it only after verified publication AND link.
      if (!retireReplacementBackup()) {
        settle(permitted() ? DownloadState::Linking : DownloadState::Paused, DownloadError::StorageFailure);
        return;
      }
      logReplacement(record_.spec, record_.previous);
    }
    settle(DownloadState::Complete);
    return;
  }
  const int n = source_->read(buffer_.data(), buffer_.size());
  if (!permitted()) {
    settle(DownloadState::Paused);
    return;
  }
  if (n < 0 || n > int(buffer_.size())) {
    fail(DownloadError::Unavailable);
    return;
  }
  if (n) {
    const auto count = static_cast<uint64_t>(n);
    if (status_.received > MaxBytes - count || (status_.totalKnown && status_.received + count > status_.total)) {
      fail(DownloadError::Invalid);
      return;
    }
    const auto free = ports_.freeBytes(ports_.context);
    if (free < Headroom || free - Headroom < count) {
      fail(DownloadError::NoSpace);
      return;
    }
    if (!artifact_.write(buffer_.data(), n)) {
      fail(DownloadError::StorageFailure);
      return;
    }
    status_.received += count;
    ++status_.sequence;
    // Persist at most once per 256 KiB, plus pause/completion. The HAL handle
    // remains open and reusable; no per-chunk allocation or directory writes.
    if (status_.received - flushed_ >= 256 * 1024) {
      if (!artifact_.flush()) {
        fail(DownloadError::StorageFailure);
        return;
      }
      flushed_ = status_.received;
    }
    return;
  }
  if (!source_->finish() || (status_.totalKnown && status_.received != status_.total)) {
    fail(DownloadError::Unavailable);
    return;
  }
  source_->close();
  source_ = nullptr;
  if (!permitted()) {
    settle(DownloadState::Paused);
    return;
  }
  status_.state = DownloadState::Verifying;
  ++status_.sequence;
  const bool checkpointed = artifact_.checkpoint();
  artifactOpen_ = false;
  if (!checkpointed) {
    fail(DownloadError::StorageFailure);
    return;
  }
  if (!verifyPath(staging_.data())) {
    if (!permitted()) {
      settle(DownloadState::Paused);
      return;
    }
    // Rejected bytes cannot become a resumable prefix. Never remove a complete
    // destination or reader sidecar; this run exclusively owns this staging.
    if (Storage.exists(staging_.data()) && !Storage.remove(staging_.data()))
      LOG_ERR("BSD", "Rejected staging retained");
    fail(DownloadError::Digest);
    return;
  }
  // Publish canonical identity evidence BEFORE renaming. A cut after rename or
  // a link retry can then verify SHA-256 without reopening protocol transport.
  record_.spec.identity.revision = computed_;
  record_.spec.bytes = status_.received;
  record_.spec.sizeKnown = true;
  status_.total = status_.received;
  status_.totalKnown = true;
  if (!save()) {
    fail(DownloadError::StorageFailure);
    return;
  }
  storage::FilePublish publish({record_.spec.path.data(), staging_.data(), backup_.data()}, artifactValid, this);
  if (!publish.resume(status_.received, status_.received) || !publish.finish()) {
    // F06 final barrier may report failure after the verified rename committed.
    if (!verifyPath(record_.spec.path.data()) || !publish.confirm()) {
      if (!restorePrevious()) LOG_ERR("BSC", "Copy rollback requires recovery before reader open");
      fail(DownloadError::StorageFailure);
      return;
    }
  }
  status_.state = DownloadState::Linking;
  ++status_.sequence;
}
DownloadStatus DownloadJobs::status(DeliveryTicket current) const { return same(current) ? status_ : DownloadStatus{}; }
DeliveryResult DownloadJobs::result(uint64_t id, DeliveryTicket current) const {
  if (!same(current) || !id || id != status_.id || record_.canceled) return DeliveryResult::Canceled;
  if (draining_ || running_) return DeliveryResult::Pending;
  return status_.state == DownloadState::Complete ? DeliveryResult::Ready : DeliveryResult::Unavailable;
}
bool DownloadJobs::retireReplacementBackup() {
  storage::FilePublish published({record_.spec.path.data(), staging_.data(), backup_.data()}, artifactValid, this);
  if (!verifyPath(record_.spec.path.data()) || !published.confirm()) return false;
  if (Storage.exists(backup_.data())) {
    FsFile file = Storage.open(backup_.data(), O_RDONLY);
    if (!file) {
      LOG_ERR("BSC", "Rollback backup open failed; retirement deferred");
      return false;
    }
    const bool owned = verify(file, true) && computed_ == record_.previous;
    const bool closed = file.close();
    if (!owned || !closed) {
      LOG_ERR("BSC", "Unverified rollback backup retained");
      return false;
    }
    if (!Storage.remove(backup_.data())) {
      LOG_ERR("BSC", "Rollback backup retirement failed; completed EPUB retained");
      return false;
    }
  }
  // Also reconciles a cut/failed barrier after removal; the complete current
  // file is durable before any old bytes are removed. No sidecar is touched.
  return published.confirm();
}
bool DownloadJobs::restorePrevious() {
  if (!nonzero(record_.previous.bytes) || Storage.exists(record_.spec.path.data())) return true;
  if (!permitted()) return false;
  // The publication helper validates the complete old/new file before moving it
  // back to the native path, and does not erase a conflicting destination.
  return storage::FilePublish::restoreBackup({record_.spec.path.data(), staging_.data(), backup_.data()}, artifactValid,
                                             this);
}
bool DownloadJobs::restoreReplacement(DeliveryTicket current) {
  if (!same(current)) return false;
  if (!nonzero(record_.previous.bytes) || Storage.exists(record_.spec.path.data())) return true;
  if (!canRecover()) return false;
  // Boot recovery is local SD work under the caller's serialized writer/open
  // gate. Network admission would activate Tailnet and park the C3 display.
  const bool restored = storage::FilePublish::restoreBackup({record_.spec.path.data(), staging_.data(), backup_.data()},
                                                            recoveryValid, this);
  status_.error = restored ? DownloadError::None : DownloadError::StorageFailure;
  ++status_.sequence;
  if (!restored) LOG_ERR("BSC", "Copy filename recovery failed; path remains blocked");
  return restored;
}
}  // namespace booksync
extern "C" bool booksync_download_jobs_link_anchor(booksync::DownloadJobs& jobs, const booksync::DownloadSpec& spec,
                                                   booksync::DownloadSource& source, booksync::DeliveryTicket owner,
                                                   booksync::storage::ScopeView scope) {
  const bool recovered = jobs.recover(scope, owner);
  const auto id = jobs.start(spec, source, owner);
  jobs.tick(owner);
  jobs.resume(id, source, owner);
  const auto result = jobs.result(id, owner);
  jobs.cancel(id, owner);
  return recovered && result == booksync::DeliveryResult::Ready;
}
