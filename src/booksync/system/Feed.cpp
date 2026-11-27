// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from CrossInk release parser and BookSync signed-OTA patches.
#include "Feed.h"

#include <cstdlib>
#include <cstring>
#include <limits>

#include "UpdateVerify.h"

namespace booksync {

namespace {

void safeCopy(char* dst, size_t dstSize, const char* src, size_t srcLen) {
  size_t n = srcLen < dstSize - 1 ? srcLen : dstSize - 1;
  memcpy(dst, src, n);
  dst[n] = '\0';
}

void copyGithubSha256Digest(char* dst, size_t dstSize, const char* value, size_t valueLen) {
  static constexpr char kSha256Prefix[] = "sha256:";
  static constexpr size_t kSha256PrefixLen = sizeof(kSha256Prefix) - 1;
  static constexpr size_t kSha256HexLen = 64;
  if (valueLen != kSha256PrefixLen + kSha256HexLen || memcmp(value, kSha256Prefix, kSha256PrefixLen) != 0) return;

  safeCopy(dst, dstSize, value + kSha256PrefixLen, valueLen - kSha256PrefixLen);
}

}  // namespace

// Keep the constructor/callback graph measured before F32 creates update jobs.
__attribute__((used, retain)) Feed::Feed(AssetMatcher assetMatcher, ReleaseVisitor releaseVisitor,
                                         void* releaseVisitorCtx)
    : parser(JsonCallbacks{this, sOnKey, sOnString, sOnNumber, sOnBool, sOnNull, sOnObjectStart, sOnObjectEnd,
                           sOnArrayStart, sOnArrayEnd}),
      assetMatcher(assetMatcher),
      releaseVisitor(releaseVisitor),
      releaseVisitorCtx(releaseVisitorCtx) {
  reset();
}

void Feed::reset() {
  parser.reset();
  invalid = false;
  closed = false;
  currentAssetSigned = false;
  position = Position::TOP_LEVEL;
  lastKey = LastKey::NONE;
  depth = 0;
  assetDepth = 0;
  releaseList = false;
  clearRelease();
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSha256[0] = '\0';
  currentAssetSize = 0;
  currentAssetSigned = false;
}

void Feed::feed(const char* data, size_t len) {
  if (data == nullptr && len) {
    invalid = true;
    return;
  }
  for (size_t i = 0; i < len && !invalid; ++i) {
    const char c = data[i];
    if (closed && c != ' ' && c != '\t' && c != '\n' && c != '\r') {
      invalid = true;
      break;
    }
    parser.feed(data + i, 1);
  }
}
void Feed::feedResponse(int status, const char* data, size_t len) {
  if (status == 200 && len) feed(data, len);
}
bool Feed::complete() const { return closed && !invalid && !parser.hasError(); }

bool Feed::isReleaseList() const { return releaseList; }
const Feed::Release& Feed::release() const { return current; }
bool Feed::foundTag() const { return current.tagFound; }
bool Feed::foundFirmware() const { return current.firmwareFound; }
const char* Feed::getTagName() const { return current.tagName; }
const char* Feed::getFirmwareUrl() const { return current.firmwareUrl; }
size_t Feed::getFirmwareSize() const { return current.firmwareSize; }
const char* Feed::getFirmwareSha256() const { return current.firmwareSha256; }

void Feed::clearRelease() { memset(&current, 0, sizeof(current)); }

void Feed::commitRelease() {
  if (releaseVisitor != nullptr && !invalid && !current.draft && current.tagFound && current.firmwareFound) {
    releaseVisitor(releaseVisitorCtx, current);
  }
}

void Feed::commitAsset() {
  const bool matchesFirmware =
      assetMatcher != nullptr ? assetMatcher(currentAssetName) : strcmp(currentAssetName, "firmware.bin") == 0;
  if (!current.firmwareFound && matchesFirmware) {
    memcpy(current.firmwareUrl, currentAssetUrl, sizeof(current.firmwareUrl));
    memcpy(current.firmwareSha256, currentAssetSha256, sizeof(current.firmwareSha256));
    current.firmwareSize = currentAssetSize;
    current.firmwareFound = true;
    current.firmwareSigned = currentAssetSigned;
    if (currentAssetSigned) memcpy(current.firmwareSignature, currentAssetSignature, sizeof(current.firmwareSignature));
  }
  currentAssetName[0] = '\0';
  currentAssetUrl[0] = '\0';
  currentAssetSha256[0] = '\0';
  currentAssetSize = 0;
  currentAssetSigned = false;
}

// -- SAX callbacks (static trampolines) -------------------------------------

void Feed::sOnKey(void* ctx, const char* key, size_t len) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == self->releaseDepth()) {
        if (len == 8 && memcmp(key, "tag_name", 8) == 0)
          self->lastKey = LastKey::TAG_NAME;
        else if (len == 16 && memcmp(key, "booksync_version", 16) == 0)
          self->lastKey = LastKey::BOOKSYNC_ID;
        else if (len == 10 && memcmp(key, "prerelease", 10) == 0)
          self->lastKey = LastKey::PRERELEASE;
        else if (len == 5 && memcmp(key, "draft", 5) == 0)
          self->lastKey = LastKey::DRAFT;
        else if (len == 6 && memcmp(key, "assets", 6) == 0)
          self->lastKey = LastKey::ASSETS;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    case Position::IN_ASSET_OBJECT:
      if (self->assetDepth == 1) {
        if (len == 4 && memcmp(key, "name", 4) == 0)
          self->lastKey = LastKey::ASSET_NAME;
        else if (len == 20 && memcmp(key, "browser_download_url", 20) == 0)
          self->lastKey = LastKey::ASSET_URL;
        else if (len == 4 && memcmp(key, "size", 4) == 0)
          self->lastKey = LastKey::ASSET_SIZE;
        else if (len == 6 && memcmp(key, "sha256", 6) == 0)
          self->lastKey = LastKey::ASSET_SHA256;
        else if (len == 16 && memcmp(key, "orbify_signature", 16) == 0)
          self->lastKey = LastKey::ASSET_SIGNATURE;
        else if (len == 6 && memcmp(key, "digest", 6) == 0)
          self->lastKey = LastKey::ASSET_DIGEST;
        else
          self->lastKey = LastKey::NONE;
      }
      break;
    default:
      break;
  }
}

void Feed::sOnString(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->lastKey) {
    case LastKey::TAG_NAME:
      if (self->position == Position::TOP_LEVEL && self->depth == self->releaseDepth()) {
        if (len >= sizeof(self->current.tagName) || memchr(value, 0, len)) self->invalid = true;
        safeCopy(self->current.tagName, sizeof(self->current.tagName), value, len);
        self->current.tagFound = true;
      }
      break;
    case LastKey::BOOKSYNC_ID:
      if (len >= sizeof(self->current.booksyncVersion))
        self->invalid = true;
      else
        safeCopy(self->current.booksyncVersion, sizeof(self->current.booksyncVersion), value, len);
      break;
    case LastKey::ASSET_SIGNATURE:
      self->currentAssetSigned = len == 328 && update_signature::decodeHex(value, self->currentAssetSignature, 164);
      break;
    case LastKey::ASSET_NAME:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetName) || memchr(value, 0, len)) self->invalid = true;
        safeCopy(self->currentAssetName, sizeof(self->currentAssetName), value, len);
      }
      break;
    case LastKey::ASSET_URL:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetUrl) || memchr(value, 0, len)) self->invalid = true;
        safeCopy(self->currentAssetUrl, sizeof(self->currentAssetUrl), value, len);
      }
      break;
    case LastKey::ASSET_SHA256:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
        if (len >= sizeof(self->currentAssetSha256) || memchr(value, 0, len)) self->invalid = true;
        safeCopy(self->currentAssetSha256, sizeof(self->currentAssetSha256), value, len);
      }
      break;
    case LastKey::ASSET_DIGEST:
      if (self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1)
        copyGithubSha256Digest(self->currentAssetSha256, sizeof(self->currentAssetSha256), value, len);
      break;
    default:
      break;
  }
  self->lastKey = LastKey::NONE;
}

void Feed::sOnNumber(void* ctx, const char* value, size_t len) {
  auto* self = static_cast<Feed*>(ctx);

  if (self->lastKey == LastKey::ASSET_SIZE && self->position == Position::IN_ASSET_OBJECT && self->assetDepth == 1) {
    size_t size = 0;
    for (size_t i = 0; i < len; ++i) {
      const unsigned digit = static_cast<unsigned>(value[i] - '0');
      if (digit > 9 || size > (std::numeric_limits<size_t>::max() - digit) / 10) {
        self->invalid = true;
        return;
      }
      size = size * 10 + digit;
    }
    self->currentAssetSize = size;
  }
  self->lastKey = LastKey::NONE;
}

void Feed::sOnBool(void* ctx, bool value) {
  auto* self = static_cast<Feed*>(ctx);
  if (self->lastKey == LastKey::PRERELEASE) self->current.prerelease = value;
  if (self->lastKey == LastKey::DRAFT) self->current.draft = value;
  self->lastKey = LastKey::NONE;
}

void Feed::sOnNull(void* ctx) { static_cast<Feed*>(ctx)->lastKey = LastKey::NONE; }

void Feed::sOnObjectStart(void* ctx) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      self->depth++;
      // Each release of a list starts from nothing, so one without a firmware asset never
      // reports the previous release's.
      if (self->releaseList && self->depth == 2) self->clearRelease();
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::IN_ASSET_OBJECT;
      self->assetDepth = 1;
      self->currentAssetName[0] = '\0';
      self->currentAssetUrl[0] = '\0';
      self->currentAssetSha256[0] = '\0';
      self->currentAssetSize = 0;
      self->currentAssetSigned = false;
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
  }
}

void Feed::sOnObjectEnd(void* ctx) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth == self->releaseDepth()) self->commitRelease();
      if (self->depth > 0) self->depth--;
      if (self->depth == 0) self->closed = true;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      if (self->assetDepth == 0) {
        self->commitAsset();
        self->position = Position::IN_ASSETS_ARRAY;
      }
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void Feed::sOnArrayStart(void* ctx) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->lastKey == LastKey::ASSETS && self->depth == self->releaseDepth()) {
        self->position = Position::IN_ASSETS_ARRAY;
      } else {
        // A document that opens with '[' is GET /releases' list of releases.
        if (self->depth == 0) self->releaseList = true;
        self->depth++;
      }
      self->lastKey = LastKey::NONE;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth++;
      self->lastKey = LastKey::NONE;
      break;
    default:
      break;
  }
}

void Feed::sOnArrayEnd(void* ctx) {
  auto* self = static_cast<Feed*>(ctx);

  switch (self->position) {
    case Position::TOP_LEVEL:
      if (self->depth > 0) self->depth--;
      if (self->depth == 0) self->closed = true;
      break;
    case Position::IN_ASSETS_ARRAY:
      self->position = Position::TOP_LEVEL;
      break;
    case Position::IN_ASSET_OBJECT:
      self->assetDepth--;
      self->lastKey = LastKey::NONE;
      break;
  }
}

}  // namespace booksync
