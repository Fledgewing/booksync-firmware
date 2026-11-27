#include "Bootstrap.h"

#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>

#include "CrossPointState.h"
#include "RecentBooksStore.h"
#include "activities/Activity.h"
#include "booksync/services/LocalLibrary.h"
#include "booksync/services/OfflineLibrary.h"
#include "booksync/ui/Home.h"
#include "components/UITheme.h"
#include "fontIds.h"

namespace booksync {
namespace {
class ShellActivity final : public Activity {
  Home home{offlineLibrary()};
  std::string bookPath;
  HalDisplay::RefreshMode initialRefresh;

 public:
  ShellActivity(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh)
      : Activity("Home", renderer, input), initialRefresh(refresh) {}
  bool isHomeActivity() const override { return true; }
  void onEnter() override {
    Activity::onEnter();
    enterLocalLibrary();
    bookPath = APP_STATE.openEpubPath;
    if (bookPath.empty()) {
      const auto& books = RECENT_BOOKS.getBooks();
      if (!books.empty()) bookPath = books.front().path;
    }
    // Entry-time validation only; no scans, writes or file I/O during paint.
    publishHomeBook(!bookPath.empty() && Storage.exists(bookPath.c_str()));
    requestUpdate();
  }
  void onExit() override {
    leaveLocalLibrary();
    publishHomeBook(false);
    Activity::onExit();
  }
  void loop() override {
    {
      RenderLock lock(RenderLock::Mode::Try);
      if (lock.ownsLock()) workLocalLibrary();
    }
    int x = 0, y = 0;
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    const int rowTop = header.y + header.height + 24;
    const bool tapped = mappedInput.wasScreenTapped(x, y) && x >= safe.x && x < safe.x + safe.width && y >= rowTop &&
                        y < rowTop + renderer.getLineHeight(UI_12_FONT_ID) + 24;
    if (mappedInput.wasReleased(MappedInputManager::Button::Confirm) || tapped) home.submit();
    std::string indexedPath;
    if (takeIndexedOpenRequest(indexedPath)) activityManager.goToReader(indexedPath);
    if (takeHomeOpenRequest()) activityManager.goToReader(bookPath);
  }
  void render(RenderLock&&) override {
    const auto value = home.snapshot();
    renderer.clearScreen();
    const auto safe = UITheme::getInstance().getScreenSafeArea(renderer, true, false);
    const auto header = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, safe);
    GUI.drawHeader(renderer, header, tr(STR_HOME));
    UITheme::drawCenteredText(renderer, safe, UI_12_FONT_ID, header.y + header.height + 24,
                              value.canOpenLastBook ? tr(STR_CONTINUE_READING) : tr(STR_NO_RECENT_BOOKS));
    const auto labels = mappedInput.mapLabels("", value.canOpenLastBook ? tr(STR_OPEN) : "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
    renderer.displayBuffer(initialRefresh);
    initialRefresh = HalDisplay::FAST_REFRESH;
  }
};
}  // namespace
Library& homeLibrary() { return offlineLibrary(); }
void showHome(GfxRenderer& renderer, MappedInputManager& input, HalDisplay::RefreshMode refresh) {
  // Activity lifetime must outlive this call and is owned by ActivityManager.
  auto shell = makeUniqueNoThrow<ShellActivity>(renderer, input, refresh);
  if (!shell) {
    LOG_ERR("BOOKSYNC", "Cannot allocate Home shell");
    return;
  }
  activityManager.replaceActivity(std::move(shell));
}
}  // namespace booksync
