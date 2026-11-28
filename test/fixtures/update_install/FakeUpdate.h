// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <cstring>

#include "../update/FeedFixture.h"
#include "booksync/system/Update.h"

namespace update_fixture {
using namespace booksync;
using namespace booksync::update;
struct FakeUpdate {
  Context state{{false, true, false}, true, true, false, false, true};
  Image image{}, previous{}, boot{};
  bool compatible = false, writeOk = true, locked = false, readingOnLock = false;
  unsigned writes = 0, restarts = 0;
  Feed::Release release{};
  Diagnostics diagnostics;
  Ports ports{this,
              [](void* p) { return static_cast<FakeUpdate*>(p)->state; },
              [](void* p, bool previous, Image& out) {
                auto& f = *static_cast<FakeUpdate*>(p);
                if (f.locked) return false;
                f.locked = true;
                if (f.readingOnLock) f.state.presentation.reading = true;
                out = previous ? f.previous : f.image;
                return true;
              },
              [](void* p) { static_cast<FakeUpdate*>(p)->locked = false; },
              [](void* p, Image& out) {
                out = static_cast<FakeUpdate*>(p)->boot;
                return true;
              },
              [](void* p, const uint8_t digest[32]) {
                auto& f = *static_cast<FakeUpdate*>(p);
                return f.compatible && !memcmp(digest, f.previous.digest, 32);
              },
              [](void* p, bool, const Image&) {
                auto& f = *static_cast<FakeUpdate*>(p);
                ++f.writes;
                return f.writeOk;
              },
              [](void* p) { ++static_cast<FakeUpdate*>(p)->restarts; }};
  Device device{Board::Pro, {42}, "firmware-x4-pro", 6553600, update_fixture::root};
  Update service{ports, device, diagnostics};
  FakeUpdate() {
#ifndef SIMULATOR
    card = {};
#else
    Storage.mkdir("/.booksync");
#endif
    Feed parser([](const char* name) { return strncmp(name, "firmware-x4-pro-", 16) == 0; });
    parser.feedResponse(200, update_fixture::feed, sizeof(update_fixture::feed) - 1);
    release = parser.release();
    // Synthetic bridge alias; the exact signed tag/asset are unchanged.
    strcpy(release.booksyncVersion, "0.12.0");
    release.booksyncVersionFound = true;
    image.board = previous.board = boot.board = Board::Pro;
    memcpy(image.layout, device.layout, 32);
    memcpy(previous.layout, device.layout, 32);
    memcpy(boot.layout, device.layout, 32);
    image.complete = previous.complete = boot.complete = true;
    image.verified = previous.verified = boot.verified = true;
    image.size = release.firmwareSize;
    previous.size = boot.size = 30;
    update_signature::decodeHex(release.firmwareSha256, image.digest, 32);
    previous.digest[0] = boot.digest[0] = 7;
  }
  Result install() { return service.install(release, {"0.11.4", "2.0.0.149"}, update_pick::Channel::Stable); }
};
template <class Expect>
void scenario(FakeUpdate& f, Expect expect) {
  f.state.presentation.reading = true;
  expect(f.install() == Result::Deferred);
  expect(f.writes == 0 && f.restarts == 0);
  f.state.presentation.reading = false;
  f.state.networkBusy = true;
  expect(f.install() == Result::Busy);
  f.state.networkBusy = false;
  f.state.activationRecovery = true;
  expect(f.install() == Result::Activation);
  f.state.activationRecovery = false;
  expect(f.install() == Result::Started && f.writes == 1);
  expect(f.service.receipt().phase == Phase::AwaitBoot);
  f.state.presentation.reading = true;
  expect(f.service.restart() == Result::Deferred && f.restarts == 0);
  f.state.presentation.reading = false;
  expect(f.service.restart() == Result::Started && f.restarts == 1);
  Update restarted(f.ports, f.device, f.diagnostics);
  expect(restarted.recover());
  expect(restarted.receipt().phase == Phase::Interrupted);  // source actually booted
  // A new fake install and boot proves completion from the exact target digest.
  expect(f.install() == Result::Busy);  // same owner cannot write twice
  expect(restarted.install(f.release, {"0.11.4", ""}, update_pick::Channel::Stable) == Result::Started);
  expect(memcmp(restarted.receipt().source, restarted.receipt().target, 32) != 0);
  f.boot = f.image;  // Only the simulated next boot changes the running image.
  Update completed(f.ports, f.device, f.diagnostics);
  expect(completed.recover());
  expect(completed.receipt().phase == Phase::Complete);
  expect(!completed.canReturn());
  f.compatible = true;
  expect(completed.canReturn());
  f.state.presentation.reading = true;
  expect(completed.returnPrevious() == Result::Deferred);
  f.state.presentation.reading = false;
  expect(completed.returnPrevious() == Result::Started);
  f.boot = f.previous;
  Update returned(f.ports, f.device, f.diagnostics);
  expect(returned.recover());
  expect(returned.receipt().phase == Phase::Returned);
  expect(!f.locked);
}
}  // namespace update_fixture
