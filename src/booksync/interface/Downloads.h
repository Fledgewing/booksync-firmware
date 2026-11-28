// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstdint>
namespace booksync {
enum class DownloadState : uint8_t { Missing, Paused, Transferring, Verifying, Linking, Complete, Canceled, Failed };
enum class DownloadError : uint8_t { None, Invalid, Busy, StorageFailure, NoSpace, Unavailable, Edition, Digest };
struct DownloadStatus {
  uint64_t id = 0, sequence = 0, received = 0, total = 0;
  DownloadState state = DownloadState::Missing;
  DownloadError error = DownloadError::None;
  bool totalKnown = false;
};
}  // namespace booksync
