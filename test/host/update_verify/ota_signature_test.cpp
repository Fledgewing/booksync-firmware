// @fixture release-signing/xteink-feed.json
// The signed-update OTA scenario (overlay patch 0050, docs/design/release-signing.md), on the host:
// the updater's own release parser reads the feed (shared/fixtures/release-signing/xteink-feed.json)
// in network-sized chunks, then the updater's check (UpdateSignature::checkDownload) decides on the
// image whose SHA-256 the staging step matched. Upstream's simulator stubs the whole updater, so
// the scenario runs here: a feed and image an attacker replaced are refused once enforced and only
// logged before. Built by run-host-tests.sh against a CrossInk checkout with the overlay applied.
#include <cstring>
#include <string>

#include "booksync/system/Feed.h"
using ReleaseJsonParser = booksync::Feed;

#include "TestSupport.h"
#include "booksync/system/UpdateVerify.h"

namespace sig = booksync::update_signature;

static const char* stem = "firmware-x4-pro";
static bool matchesDevice(const char* name) {
  const size_t n = strlen(stem);
  return strncmp(name, stem, n) == 0 && name[n] == '-' && strstr(name, ".bin") != nullptr;
}

static void keepFirst(void* ctx, const ReleaseJsonParser::Release& r) {
  auto* out = static_cast<ReleaseJsonParser::Release*>(ctx);
  if (!out->tagFound) *out = r;
}

static std::string readText(const std::string& path) {
  std::string out;
  FILE* f = std::fopen(path.c_str(), "rb");
  if (!f) return out;
  char buf[4096];
  size_t n;
  while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) out.append(buf, n);
  std::fclose(f);
  return out;
}

static void replaceAll(std::string& s, const std::string& from, const std::string& to) {
  for (size_t at = s.find(from); at != std::string::npos; at = s.find(from, at + to.size())) s.replace(at, from.size(), to);
}

// What the updater would do with this feed and a staged image whose SHA-256 matched the feed's
// digest: the check's result for this reader's image.
static sig::Result update(const std::string& feed, const char* root, const char* device = "firmware-x4-pro") {
  stem = device;
  ReleaseJsonParser::Release picked{};
  ReleaseJsonParser parser(matchesDevice, keepFirst, &picked);
  // A repository/asset redirect can contain JSON or HTML. Neither may reach the
  // release parser, even when a redirect body arrives over multiple callbacks.
  for (int status : {301, 302, 307, 404}) {
    const std::string body = "<html>Moved</html>";
    parser.feedResponse(status, body.data(), body.size());
  }
  for (size_t i = 0; i < feed.size(); i += 61)
    parser.feedResponse(200, feed.data() + i, std::min<size_t>(61, feed.size() - i));
  CHECK(parser.isReleaseList() && picked.tagFound && picked.firmwareFound);
  return sig::checkDownload(root, 1, picked.tagName, picked.firmwareUrl, picked.firmwareSha256,
                            picked.firmwareSigned ? picked.firmwareSignature : nullptr, stem);
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s shared/fixtures/release-signing\n", argv[0]);
    return 2;
  }
  const std::string dir = argv[1];
  JsonDocument cases;
  if (!loadJsonFile((dir + "/cases.json").c_str(), cases)) return 1;
  const std::string root = cases["root_public_key"].as<std::string>();
  const std::string goodSha = cases["cases"][0]["sha256"].as<std::string>();
  const std::string tamperedSha = cases["cases"][1]["sha256"].as<std::string>();
  const std::string feed = readText(dir + "/xteink-feed.json");
  CHECK(!feed.empty());

  // Certain non-JSON redirect bodies poison the old unfiltered parser. The
  // transport still follows the redirect; the status filter protects parsing.
  ReleaseJsonParser unfiltered(matchesDevice);
  const std::string redirectBody = "301: Moved Permanently";
  unfiltered.feed(redirectBody.data(), redirectBody.size());
  unfiltered.feed(feed.data(), feed.size());
  CHECK(!unfiltered.foundFirmware());

  // The release as published: signed, installs either way.
  CHECK(update(feed, root.c_str()) == sig::Result::Ok);
  CHECK(sig::accepted(update(feed, root.c_str()), true));

  // Repository renames and old download URLs have identical signing inputs.
  std::string oldRepo = feed;
  replaceAll(oldRepo, "Fledgewing/BookSync", "Fledgewing/Orbify");
  CHECK(oldRepo != feed);
  CHECK(update(oldRepo, root.c_str()) == sig::Result::Ok);

  // A valid signature for another reader must not be selected through a changed display name.
  JsonDocument relabeledDoc;
  CHECK(!deserializeJson(relabeledDoc, feed));
  auto assets = relabeledDoc[0]["assets"].as<JsonArray>();
  assets.remove(0);
  assets[0]["name"] = "firmware-x4-classic-v2.0.0.150.bin";
  std::string relabeled;
  serializeJson(relabeledDoc, relabeled);
  CHECK(update(relabeled, root.c_str(), "firmware-x4-classic") == sig::Result::BadSignature);
  CHECK(!sig::accepted(update(relabeled, root.c_str(), "firmware-x4-classic"), true));

  // An attacker swaps the image and the feed's digest to match it: the staged image passes its
  // SHA-256 check, but the signature covers the original digest.
  std::string swapped = feed;
  replaceAll(swapped, "sha256:" + goodSha, "sha256:" + tamperedSha);
  const sig::Result tampered = update(swapped, root.c_str());
  CHECK(tampered == sig::Result::BadSignature);
  CHECK(!sig::accepted(tampered, true));  // enforced: refused before the flash is touched
  CHECK(sig::accepted(tampered, false));  // not enforced yet: logged, installed
  CHECK(std::string(sig::describe(tampered)) == "signature invalid");

  // ... or strips the signature.
  std::string stripped = feed;
  const size_t at = stripped.find(",\n        \"orbify_signature\"");
  CHECK(at != std::string::npos);
  stripped.erase(at, stripped.find('"', stripped.find(": \"", at) + 3) + 1 - at);
  CHECK(update(stripped, root.c_str()) == sig::Result::Unsigned);
  CHECK(!sig::accepted(update(stripped, root.c_str()), true));

  // ... or offers the same signed image as a newer release.
  std::string renamed = feed;
  replaceAll(renamed, "xteink-v2.0.0.150\"", "xteink-v2.0.0.999\"");
  CHECK(update(renamed, root.c_str()) == sig::Result::BadSignature);

  // A reader with no signature on its image (the x4-classic asset) is unsigned.
  CHECK(update(feed, root.c_str(), "firmware-x4-classic") == sig::Result::Unsigned);
  // A build without the root key can't check, so it only logs.
  CHECK(update(swapped, "") == sig::Result::NoRoot);
  CHECK(sig::accepted(sig::Result::NoRoot, false));
  // A root that never certified this release key refuses it.
  CHECK(update(feed, cases["other_root_public_key"]) == sig::Result::BadCertificate);
  return report("ota_signature");
}
