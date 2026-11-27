#!/usr/bin/env python3
"""Exercise production Plugins and asset-download network resource lifecycles.

The host cannot reproduce the C3 allocator or TLS peak. These checks ensure
hardware enters a fresh boot once, resumes without looping, and leaves with a
full boot after releasing network resources. The simulator stays in-process.
"""

from pathlib import Path
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[2]
manager = (ROOT / "src/activities/ActivityManager.cpp").read_text()
plugins = (ROOT / "src/activities/plugins/PluginCatalogActivity.cpp").read_text()
assets = (ROOT / "src/activities/settings/AssetDownloadActivity.cpp").read_text()
boot = (ROOT / "src/main.cpp").read_text()


def function(source, signature):
    start = source.index(signature)
    brace = source.index('{', start)
    depth, end = 1, brace + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]


route = function(manager, "bool ActivityManager::goToPlugins(")
exit_method = function(plugins, "void PluginCatalogActivity::onExit()")
asset_exit = function(assets, "void AssetDownloadActivity::onExit()")
case_start = boot.index("      case NetworkBootTarget::PLUGINS:")
case_end = boot.index("        break;", case_start) + len("        break;")

harness = r'''
#include <cassert>
#include <memory>
#include <string>
#include <utility>
#include <vector>
#include "SilentRestart.h"
#define LOG_ERR(...) ((void)0)
std::vector<std::string> events;
[[maybe_unused]] struct {} renderer;
struct {
  void setReaderTouchscreenOverride(bool enabled) {
    assert(!enabled);
    events.push_back("input");
  }
} mappedInput;
[[maybe_unused]] struct {
  unsigned getFreeHeap() { return 100000; }
  unsigned getMaxAllocHeap() { return 100000; }
} ESP;
[[maybe_unused]] struct {
  void begin(decltype(renderer)&) { events.push_back("fonts"); }
} sdFontSystem;
struct {
  void remove(const char*) { events.push_back("temp"); }
} Storage;
constexpr const char* BROWSE_TMP_PATH = "/catalog.tmp";
struct CatalogActivity {
  void onExit() { events.push_back("wifi"); }
};
struct Activity {
  void onExit() { events.push_back("activity"); }
};
constexpr int WIFI_OFF = 0;
struct {
  void disconnect(bool erase) {
    assert(!erase);
    events.push_back("disconnect");
  }
  void mode(int mode) {
    assert(mode == WIFI_OFF);
    events.push_back("off");
  }
} WiFi;
struct AssetDownloadActivity : Activity {
  struct Popup {
    void clear() { events.push_back("popup"); }
  } popup_;
  bool catalogReady_ = false;
  void onExit();
};
struct Session {
  void reset() { events.push_back("tls"); }
};
struct PluginCatalogActivity : CatalogActivity {
  PluginCatalogActivity(decltype(renderer)&, decltype(mappedInput)&, bool) {}
  Session session;
  void clearItems() { events.push_back("rows"); }
  void onExit();
};
bool failAllocation = false;
unsigned allocations = 0, restarts = 0;
NetworkBootTarget destination = NetworkBootTarget::OTA;
bool silentRestart() { events.push_back("home"); return true; }
bool silentRestartToNetwork(NetworkBootTarget target, uint32_t payload) {
  ++restarts;
  destination = target;
  assert(payload == 0);
  return true;
}
template <class T, class... Args>
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  ++allocations;
  if (failAllocation) return nullptr;
  return std::make_unique<T>(std::forward<Args>(args)...);
}
struct ActivityManager {
  std::unique_ptr<PluginCatalogActivity> pending;
  bool goToPlugins(bool networkBootReady = false);
  void replaceActivity(std::unique_ptr<PluginCatalogActivity> activity) {
    pending = std::move(activity);
  }
} activityManager;
'''

dispatch = """
bool resume(NetworkBootTarget target) {
  bool launched = false;
  switch (target) {
""" + boot[case_start:case_end] + """
    default: break;
  }
  return launched;
}
"""

cases = r'''
int main() {
  assert(isNetworkBootTargetValue(static_cast<uint32_t>(NetworkBootTarget::PLUGINS)));
  assert(!isNetworkBootTargetValue(0));
  assert(!isNetworkBootTargetValue(10));
  assert(!isNetworkBootTargetValue(UINT32_MAX));
  // BookSync retires this product route for both fresh and resumed calls.
  assert(!activityManager.goToPlugins());
  assert(!activityManager.goToPlugins(true));
  assert(!resume(NetworkBootTarget::PLUGINS));
  assert(restarts == 0 && allocations == 0 && !activityManager.pending);

  // Asset downloads must restore normal boot even if Wi-Fi was cancelled or
  // setup failed before the catalog selection was ready.
  for (bool loaded : {false, true}) {
    events.clear();
    AssetDownloadActivity assets;
    assets.catalogReady_ = loaded;
    assets.onExit();
    assert(!assets.catalogReady_);
    std::vector<std::string> expected{"popup", "disconnect", "off"};
#ifdef SIMULATOR
    expected.push_back("fonts");
#endif
    expected.insert(expected.end(), {"input", "activity"});
#ifndef SIMULATOR
    expected.push_back("home");
#endif
    assert(events == expected);
  }
}
'''

with tempfile.TemporaryDirectory(prefix="crossink-plugin-boot-") as directory:
    source = Path(directory) / "test.cpp"
    source.write_text(harness + route + "\n" + exit_method + "\n" + asset_exit + "\n" + dispatch + cases)
    for platform, defines in (("hardware", []), ("simulator", ["-DSIMULATOR"])):
        executable = Path(directory) / platform
        subprocess.run(["c++", "-std=c++20", "-Wall", "-Wextra", "-Werror", *defines,
                        "-I", str(ROOT / "src"), str(source), "-o", str(executable)], check=True)
        subprocess.run([str(executable)], check=True)
print("Retired plugin route and retained asset-download resource restoration passed (hardware and simulator paths).")
