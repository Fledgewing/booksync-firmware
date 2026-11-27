// SPDX-License-Identifier: GPL-3.0-or-later
// Derived from CrossInk release parser and BookSync signed-OTA patches.
#pragma once

#include <cstddef>
#include <cstdint>

#include "StreamingJsonParser.h"

namespace booksync {

// Bounded streaming decoder. Visitor values are provisional until complete() and
// transport and detached-feed trust checks succeed. Reset the parser AND the
// caller's selection on retry.
class Feed {
 public:
  using AssetMatcher = bool (*)(const char* assetName);

  // One release as read from the response: its tag, and the first firmware asset
  // assetMatcher accepted.
  struct Release {
    char tagName[128];
    char booksyncVersion[48];
    char firmwareUrl[512];
    char firmwareSha256[65];
    size_t firmwareSize;
    bool tagFound;
    bool firmwareFound;
    bool prerelease;
    bool draft;
    uint8_t firmwareSignature[164];
    bool firmwareSigned;
  };

  // GitHub's GET /releases answers with an array of releases rather than one. The parser
  // reports them one at a time: the visitor runs as each release object closes, for those
  // with a tag and a matching firmware asset, in input order. The caller owns selection.
  using ReleaseVisitor = void (*)(void* ctx, const Release& release);

  explicit Feed(AssetMatcher assetMatcher = nullptr, ReleaseVisitor releaseVisitor = nullptr,
                void* releaseVisitorCtx = nullptr);

  Feed(const Feed&) = delete;
  Feed& operator=(const Feed&) = delete;

  void reset();
  void feed(const char* data, size_t len);
  void feedResponse(int status, const char* data, size_t len);
  bool complete() const;

  // True once the response turned out to be an array of releases. The accessors below then
  // describe the last release in it; the visitor has seen each one.
  bool isReleaseList() const;

  const Release& release() const;
  bool foundTag() const;
  bool foundFirmware() const;
  const char* getTagName() const;
  const char* getFirmwareUrl() const;
  size_t getFirmwareSize() const;
  const char* getFirmwareSha256() const;

 private:
  enum class Position : uint8_t {
    TOP_LEVEL,
    IN_ASSETS_ARRAY,
    IN_ASSET_OBJECT,
  };

  enum class LastKey : uint8_t {
    NONE,
    TAG_NAME,
    BOOKSYNC_ID,
    PRERELEASE,
    DRAFT,
    ASSETS,
    ASSET_NAME,
    ASSET_URL,
    ASSET_SIZE,
    ASSET_SHA256,
    ASSET_DIGEST,
    ASSET_SIGNATURE,
  };

  static void sOnKey(void* ctx, const char* key, size_t len);
  static void sOnString(void* ctx, const char* value, size_t len);
  static void sOnNumber(void* ctx, const char* value, size_t len);
  static void sOnBool(void* ctx, bool value);
  static void sOnNull(void* ctx);
  static void sOnObjectStart(void* ctx);
  static void sOnObjectEnd(void* ctx);
  static void sOnArrayStart(void* ctx);
  static void sOnArrayEnd(void* ctx);

  void commitAsset();
  void commitRelease();
  void clearRelease();
  // Object depth at which a release's own keys sit: 1 in a single release, 2 in a list.
  uint8_t releaseDepth() const { return releaseList ? 2 : 1; }

  StreamingJsonParser parser;
  AssetMatcher assetMatcher;
  ReleaseVisitor releaseVisitor;
  void* releaseVisitorCtx;

  Position position;
  LastKey lastKey;
  uint8_t depth;
  uint8_t assetDepth;
  bool releaseList;

  Release current;

  char currentAssetName[96];
  char currentAssetUrl[512];
  char currentAssetSha256[65];
  size_t currentAssetSize;
  uint8_t currentAssetSignature[164];
  bool currentAssetSigned;
  bool invalid;
  bool closed;
};

}  // namespace booksync
