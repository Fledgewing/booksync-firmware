// SPDX-License-Identifier: GPL-3.0-or-later
#include <Logging.h>
#include <Memory.h>

#include "NativeProgress.h"
#include "NativeState.h"

static_assert(sizeof(booksync::server::NativeProgress) <= 2 * 1024);
static_assert(sizeof(booksync::server::NativeState) <= 2 * 1024);

// Budget root only, with no startup caller. Include provider construction,
// concrete vtables and durable native application in hardware image receipts
// before F46c2 allocates these optional workspaces on the sole worker.
extern "C" bool booksync_native_application_link(
    booksync::Account& account, booksync::storage::BindingView binding, booksync::storage::SourceFile::Closed closed,
    void* context, booksync::server::ProgressCapture& progressCapture, booksync::server::NativeStateStore& stateCapture,
    booksync::server::NativeStateStore& stateStore, booksync::storage::PositionStore& positionStore,
    const booksync::storage::JournalEntry& entry, const booksync::storage::PositionValue& place,
    booksync::server::ProgressInput& progressInput, booksync::server::StateInput& stateInput,
    const booksync::server::ProgressValues& progressReply, const booksync::server::StateValues& stateReply,
    char* scratch, size_t capacity) {
  auto source = makeUniqueNoThrow<booksync::storage::SourceFile>(binding, closed, context);
  if (!source) {
    LOG_ERR("BSE", "Cannot allocate source proof workspace");
    return false;
  }
  auto progress = makeUniqueNoThrow<booksync::server::NativeProgress>(account, binding.scope, *source, progressCapture,
                                                                      positionStore, scratch, capacity);
  auto state =
      makeUniqueNoThrow<booksync::server::NativeState>(account, binding.scope, *source, stateCapture, stateStore);
  if (!progress || !state) {
    LOG_ERR("BSE", "Cannot allocate native application workspace");
    return false;
  }
  const bool progressFrozen = progress->freeze(entry, place, false);
  const bool progressLoaded = progress->load(entry, progressInput);
  const bool progressApplied = progress->apply(entry, progressInput, progressReply);
  const bool stateFrozen = state->freeze(entry);
  const bool stateLoaded = state->load(entry, stateInput);
  const bool stateApplied = state->apply(entry, stateInput, stateReply);
  return progressFrozen && progressLoaded && progressApplied && stateFrozen && stateLoaded && stateApplied;
}
