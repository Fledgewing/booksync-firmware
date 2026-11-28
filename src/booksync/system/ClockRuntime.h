// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace booksync {
// Main-loop publication only; SNTP callbacks never call Library or render.
void publishRuntimeClock();
// Pure proof sampling for a just-in-time draw; no NVS persistence or sync work.
void sampleRuntimeClock();
}  // namespace booksync
