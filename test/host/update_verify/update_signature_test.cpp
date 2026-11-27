// @fixture release-signing/cases.json
// @fixture release-signing/SIGNATURES-firmware
// @fixture release-signing/SIGNATURES-plugin
// @fixture release-signing/SIGNATURES-plugin-other-root
// @fixture release-signing/SIGNATURES-plugin-rotated
// Host test for signed updates (orbify/core/Ed25519Verify, orbify/core/UpdateSignature;
// docs/design/release-signing.md): SHA-512 and RFC 8032 known answers, then every case in
// shared/fixtures/release-signing/cases.json, the cases the companion's verifier checks too:
// a valid signature, a tampered image, another version, a wrong root, a rotated and a revoked key.
#include <cstring>
#include <string>
#include <vector>

#include "booksync/system/Ed25519Verify.h"
#include "TestSupport.h"
#include "booksync/system/UpdateVerify.h"

namespace sig = booksync::update_signature;
using booksync::ed25519::sha512;
using booksync::ed25519::verify;

static std::vector<uint8_t> hex(const char* h) {
  std::vector<uint8_t> out(strlen(h) / 2);
  CHECK(sig::decodeHex(h, out.data(), out.size()));
  return out;
}

static std::string toHex(const uint8_t* b, size_t n) {
  static const char* d = "0123456789abcdef";
  std::string s;
  for (size_t i = 0; i < n; ++i) {
    s += d[b[i] >> 4];
    s += d[b[i] & 15];
  }
  return s;
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

// The 164 bytes the feed carries for `asset`, from a release's SIGNATURES file (empty: none).
static std::vector<uint8_t> signatureFor(const std::string& signatures, const std::string& asset) {
  std::string cert, line, sigHex;
  size_t pos = 0;
  while (pos < signatures.size()) {
    const size_t end = signatures.find('\n', pos);
    line = signatures.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
    pos = end == std::string::npos ? signatures.size() : end + 1;
    if (line.rfind("release-key ", 0) == 0) cert = line.substr(12);
    const size_t sp = line.find("  ");
    if (sp != std::string::npos && line.substr(sp + 2) == asset) sigHex = line.substr(0, sp);
  }
  if (cert.empty() || sigHex.empty()) return {};
  return hex((cert + sigHex).c_str());
}

static void knownAnswers() {
  struct {
    std::vector<uint8_t> message;
    const char* digest;
  } shas[] = {
      {{}, "cf83e1357eefb8bdf1542850d66d8007d620e4050b5715dc83f4a921d36ce9ce47d0d13c5d85f2b0ff8318d2877eec2f63b931bd47417a81a538327af927da3e"},
      {{'a', 'b', 'c'}, "ddaf35a193617abacc417349ae20413112e6fa4e89a97ea20a9eeee64b55d39a2192992a274fc1a836ba3c23a3feebbd454d4423643ce80e2a9ac94fa54ca49f"},
  };
  for (auto& t : shas) {
    uint8_t out[64];
    sha512(t.message.data(), t.message.size(), out);
    CHECK_NAMED(t.digest, toHex(out, 64) == t.digest);
  }
  // 119 bytes: the padding spills into a second block; 200 bytes: one full block, then the tail.
  for (size_t n : {119, 200}) {
    std::vector<uint8_t> m(n);
    for (size_t i = 0; i < n; ++i) m[i] = (uint8_t)i;
    uint8_t out[64];
    sha512(m.data(), m.size(), out);
    CHECK(toHex(out, 64) == (n == 119 ? "43e497279c2ce805903a33b54b746ea92d607f7c4807986c849823b81097a9099b5896ac7cc66df3a93edc8a91b6f3971d6c7f5688daf635737760bd080e27b3"
                                      : "986058e9895e2c2ab8f9e8cbdf801db12a44842a56a91d5a4e87b1fc98b293722c4664142e42c3c551ff898646268cd92b84ed230b8c94bed7798d4f27cd7465"));
  }

  // RFC 8032 §7.1, tests 1-3.
  struct {
    const char *pk, *msg, *sig;
  } rfc[] = {
      {"d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a", "",
       "e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e065224901555fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b"},
      {"3d4017c3e843895a92b70aa74d1b7ebc9c982ccf2ec4968cc0cd55f12af4660c", "72",
       "92a009a9f0d4cab8720e820b5f642540a2b27b5416503f8fb3762223ebdb69da085ac1e43e15996e458f3613d0f11d8c387b2eaeb4302aeeb00d291612bb0c00"},
      {"fc51cd8e6218a1a38da47ed00230f0580816ed13ba3303ac5deb911548908025", "af82",
       "6291d657deec24024827e69c3abe01a30ce548a284743a445e3680d7db5ac3ac18ff9b538d16f290ae67f760984dc6594a7c15e9716ed28dc027beceea1ec40a"},
  };
  for (auto& t : rfc) {
    auto pk = hex(t.pk), msg = hex(t.msg), s = hex(t.sig);
    CHECK_NAMED(t.sig, verify(s.data(), msg.data(), msg.size(), pk.data()));
    // Any flipped bit in the signature, the message or the key fails.
    auto bad = s;
    bad[5] ^= 1;
    CHECK(!verify(bad.data(), msg.data(), msg.size(), pk.data()));
    bad = s;
    bad[40] ^= 1;
    CHECK(!verify(bad.data(), msg.data(), msg.size(), pk.data()));
    auto longer = msg;
    longer.push_back(0);
    CHECK(!verify(s.data(), longer.data(), longer.size(), pk.data()));
    auto otherKey = pk;
    otherKey[3] ^= 4;
    CHECK(!verify(s.data(), msg.data(), msg.size(), otherKey.data()));
  }
  // S + L verifies the same equation but is not canonical: refused (RFC 8032 §5.1.7).
  {
    auto pk = hex(rfc[0].pk), s = hex(rfc[0].sig);
    static const uint8_t L[32] = {0xed, 0xd3, 0xf5, 0x5c, 0x1a, 0x63, 0x12, 0x58, 0xd6, 0x9c, 0xf7,
                                  0xa2, 0xde, 0xf9, 0xde, 0x14, 0, 0, 0, 0, 0, 0, 0, 0,
                                  0, 0, 0, 0, 0, 0, 0, 0x10};
    int carry = 0;
    for (int i = 0; i < 32; ++i) {
      const int v = s[32 + i] + L[i] + carry;
      s[32 + i] = (uint8_t)v;
      carry = v >> 8;
    }
    CHECK(!verify(s.data(), nullptr, 0, pk.data()));
  }
}

static void fixtureCases(const std::string& dir) {
  JsonDocument doc;
  if (!loadJsonFile((dir + "/cases.json").c_str(), doc)) {
    ++test_support::failed;
    return;
  }
  auto root = hex(doc["root_public_key"].as<const char*>());
  auto otherRoot = hex(doc["other_root_public_key"].as<const char*>());
  for (JsonObject c : doc["cases"].as<JsonArray>()) {
    const char* name = c["name"];
    const auto s = signatureFor(readText(dir + "/" + c["signatures"].as<std::string>()), c["asset"].as<std::string>());
    // An asset SIGNATURES has no line for is unsigned.
    const uint8_t* bytes = s.size() == sig::kSignatureBytes ? s.data() : nullptr;
    const sig::Trust trust{root.data(), c["min_serial"].as<uint32_t>()};
    const sig::Result r = sig::check(bytes, trust, c["tag"], c["asset"], c["sha256"]);
    CHECK_NAMED(name, (r == sig::Result::Ok) == c["ok"].as<bool>());
    // Enforced, only a valid signature installs; not enforced, everything installs (and is logged).
    CHECK_NAMED(name, sig::accepted(r, true) == c["ok"].as<bool>());
    CHECK_NAMED(name, sig::accepted(r, false));
    // Upper-case digests are the same digest.
    std::string upper = c["sha256"].as<std::string>();
    for (char& ch : upper) ch = (char)toupper(ch);
    CHECK_NAMED(name, (sig::check(bytes, trust, c["tag"], c["asset"], upper.c_str()) == sig::Result::Ok) == c["ok"].as<bool>());
    // Under the other root, only the key it certified is valid.
    const bool otherRootsKey = c["signatures"].as<std::string>() == "SIGNATURES-plugin-other-root";
    const sig::Result underOther = sig::check(bytes, {otherRoot.data(), 1}, c["tag"], c["asset"], c["sha256"]);
    CHECK_NAMED(name, underOther == (bytes == nullptr ? sig::Result::Unsigned
                                     : otherRootsKey  ? sig::Result::Ok
                                                      : sig::Result::BadCertificate));
  }
  // The causes, named.
  const auto plugin = signatureFor(readText(dir + "/SIGNATURES-plugin"), "orbit.koplugin.zip");
  const char* pz = doc["cases"][4]["sha256"];
  CHECK(sig::check(plugin.data(), {root.data(), 2}, "v0.9.0", "orbit.koplugin.zip", pz) == sig::Result::Revoked);
  CHECK(sig::check(plugin.data(), {root.data(), 1}, "v0.9.1", "orbit.koplugin.zip", pz) == sig::Result::BadSignature);
  CHECK(sig::check(nullptr, {root.data(), 1}, "v0.9.0", "orbit.koplugin.zip", pz) == sig::Result::Unsigned);
  CHECK(sig::check(plugin.data(), {nullptr, 1}, "v0.9.0", "orbit.koplugin.zip", pz) == sig::Result::NoRoot);
  CHECK(sig::check(plugin.data(), {root.data(), 1}, "v0.9.0", "orbit.koplugin.zip", nullptr) == sig::Result::BadSignature);
  CHECK(sig::accepted(sig::Result::NoRoot, false) && !sig::accepted(sig::Result::NoRoot, true));
  CHECK(std::string(sig::describe(sig::Result::Unsigned)) == "unsigned");
  // decodeHex wants exactly the length asked for.
  uint8_t k[32];
  CHECK(!sig::decodeHex("", k, 32));
  CHECK(!sig::decodeHex(doc["root_public_key"].as<std::string>().substr(0, 62).c_str(), k, 32));
  CHECK(!sig::decodeHex((doc["root_public_key"].as<std::string>() + "00").c_str(), k, 32));
}

int main(int argc, char** argv) {
  if (argc < 2) {
    std::fprintf(stderr, "usage: %s shared/fixtures/release-signing\n", argv[0]);
    return 2;
  }
  knownAnswers();
  fixtureCases(argv[1]);
  std::printf("update_signature: %d passed, %d failed\n", test_support::passed, test_support::failed);
  return test_support::failed ? 1 : 0;
}
