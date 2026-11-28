// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
namespace booksync {
// Main-loop publication only; SNTP callbacks never call Library or render.
void publishRuntimeClock();
}  // namespace booksync
