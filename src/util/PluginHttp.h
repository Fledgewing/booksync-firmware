#pragma once

#include <ArduinoJson.h>
#include <WString.h>

#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "PluginHttpPolicy.h"

namespace freeink {
class SecureHttpClient;
}

// Shared template, token/config storage, and bounded HTTP helpers for plugins.
namespace pluginhttp {

using Headers = std::vector<std::pair<std::string, std::string>>;

// --- JSON / template helpers ---

// Splits a dotted path ("authors.0.name") into segments.
void splitPath(const std::string& dotted, std::vector<std::string>& out);

// Resolves a dotted path against a parsed document.
JsonVariantConst resolvePath(JsonVariantConst node, const std::string& dotted);

// String or integral variant as text; "" for anything else.
std::string variantToString(JsonVariantConst v);

// Replaces every occurrence of `key` in `s` with `value`.
void substituteAll(std::string& s, const char* key, const std::string& value);

// Reads a manifest "headers" object into name/value pairs.
void readHeaders(JsonVariantConst node, Headers& out);

// One declarative HTTP request as manifests express it. Shared by the catalog
// auth/poll groups and the event handlers so the {url, method, body, headers}
// shape is parsed in exactly one place.
struct RequestSpec {
  std::string url, method, body;
  Headers headers;
};

// Fills `out` from a manifest request node ({url, method, body, headers}).
void readRequest(JsonVariantConst node, const char* defaultMethod, RequestSpec& out);

// Percent-encodes a query value ('/' included).
std::string urlEncodeQuery(const std::string& s);

// Escape spaces, controls and UTF-8 bytes while retaining URL delimiters and existing escapes.
std::string escapeUrl(const std::string& url);

// --- token / config files ---

// A manifest file path: relative paths live in the plugin's own folder
// ("token.json" -> "<pluginDir>/token.json"); absolute and empty pass through.
std::string inPluginDir(const std::string& pluginDir, const char* path);

// Reads the token at `path` (dotted) from the JSON file at `file`.
// An empty `file` means a token-less plugin: returns true with `out` empty.
bool loadTokenFromFile(const std::string& file, const std::string& path, std::string& out);

// Writes `value` at `path` into `file`, building the nesting the read path
// expects (numeric segments unsupported). Removes the file on a short write so
// a truncated token is never persisted.
bool saveTokenToFile(const std::string& file, const std::string& path, const std::string& value);

// Loads a flat JSON config file into {cfg.KEY} pairs. Missing file = empty.
void loadConfigFile(const std::string& file, Headers& out);

bool validHeaders(const Headers& headers);

}  // namespace pluginhttp
