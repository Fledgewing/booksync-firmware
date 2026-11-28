// SPDX-License-Identifier: GPL-3.0-or-later
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <components/keyboard/keyboard.h>
#include <qrcode.h>

#include <cstring>

#include "AccountScreen.h"
#include "activities/Activity.h"
#include "activities/ActivityManager.h"
#include "booksync/ui/account/AccountForm.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
namespace fui = freeink::ui;
#ifdef SIMULATOR
uint32_t formPaints = 0;
#endif
class AccountFormActivity final : public Activity {
  AccountForm form;
  // One foreground-lifetime workspace; no allocation or QR encoding in paint.
  uint8_t qrBytes[512]{};
  QRCode qr{};
  uint64_t qrRevision = 0;
  bool qrReady = false, shifted = false, symbols = false;
  uint8_t control = 0;
  Rect safe() const { return UITheme::getInstance().getScreenSafeArea(renderer, true, false); }
  int formContentTop() const {
    const auto s = safe();
    const auto h = UITheme::getHeaderRect(renderer, UITheme::getInstance().getMetrics().headerHeight, s);
    return h.y + h.height + 12;
  }
  Rect keyboardBounds() const {
    const auto s = safe();
    const int height = renderer.getLineHeight(UI_10_FONT_ID) * 5 + 130;
    return Rect{s.x + 8, s.y + s.height - 48 - height, s.width - 16, height};
  }
  Rect fieldBounds(uint8_t index) const {
    const auto s = safe();
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 22;
    return Rect{s.x + 16, formContentTop() + 132 + index * (height + 8), s.width - 32, height};
  }
  Rect actionBounds(uint8_t index) const {
    const auto s = safe();
    const int height = renderer.getLineHeight(UI_10_FONT_ID) + 24;
    const bool manual = form.view().scene == AccountScene::Manual;
    const int y = manual ? keyboardBounds().y - height - 12 : s.y + s.height - 56 - form.actions() * (height + 8);
    if (manual) return Rect{s.x + 16 + index * (s.width / 2), y, s.width / 2 - 24, height};
    return Rect{s.x + 16, y + index * (height + 8), s.width - 32, height};
  }
  const fui::KeyboardLayout& formKeyboardLayout() const {
    return fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted, symbols, true);
  }
  uint8_t keyCount() const {
    uint8_t count = 0;
    for (uint8_t r = 0; r < formKeyboardLayout().rowCount; ++r) count += formKeyboardLayout().rows[r].count;
    return count;
  }
  // SDK key tables supply labels, output and controls. One geometry function is
  // shared by touch hit testing and drawing on every profile.
  Rect keyBounds(uint8_t index) const {
    const auto b = keyboardBounds();
    const auto& l = formKeyboardLayout();
    const int height = b.height / l.rowCount;
    for (uint8_t r = 0; r < l.rowCount; ++r) {
      const auto& row = l.rows[r];
      int units = row.insetUnits * 2;
      for (uint8_t c = 0; c < row.count; ++c) units += row.keys[c].widthUnits;
      if (index < row.count) {
        int before = row.insetUnits;
        for (uint8_t c = 0; c < index; ++c) before += row.keys[c].widthUnits;
        return Rect{b.x + b.width * before / units, b.y + r * height, b.width * row.keys[index].widthUnits / units - 3,
                    height - 4};
      }
      index -= row.count;
    }
    return Rect{};
  }
  const fui::KeyboardKey* keyAt(uint8_t index) const {
    for (uint8_t r = 0; r < formKeyboardLayout().rowCount; ++r) {
      const auto& row = formKeyboardLayout().rows[r];
      if (index < row.count) return &row.keys[index];
      index -= row.count;
    }
    return nullptr;
  }
  static bool inside(Rect r, int x, int y) { return x >= r.x && x < r.x + r.width && y >= r.y && y < r.y + r.height; }
  void prepareQr() {
    if (form.view().revision == qrRevision) return;
    qrRevision = form.view().revision;
    qrReady = false;
    if ((form.view().scene == AccountScene::Phone || form.view().scene == AccountScene::Wait) && form.view().link[0]) {
      qrReady = qrcode_getBufferSize(10) <= sizeof(qrBytes) &&
                qrcode_initText(&qr, qrBytes, 10, ECC_LOW, form.view().link.data()) == 0;
      if (!qrReady) LOG_ERR("BSA", "Cannot encode public approval link");
    }
  }
  const char* title() const {
    switch (form.view().scene) {
      case AccountScene::Manual:
        return tr(STR_ACCOUNT_MANUAL_TITLE);
      case AccountScene::Busy:
        return tr(STR_ACCOUNT_BUSY_TITLE);
      case AccountScene::Phone:
        return tr(STR_ACCOUNT_PHONE_TITLE);
      case AccountScene::Wait:
        return tr(STR_ACCOUNT_WAIT_TITLE);
      case AccountScene::Expired:
        return tr(STR_ACCOUNT_EXPIRED_TITLE);
      case AccountScene::Failed:
        return tr(STR_ACCOUNT_FAILED_TITLE);
      case AccountScene::TwoStep:
        return tr(STR_ACCOUNT_TWO_STEP_TITLE);
      case AccountScene::Old:
        return tr(STR_ACCOUNT_OLD_TITLE);
      default:
        return tr(STR_ACCOUNT_YOUR_SERVER);
    }
  }
  const char* body() const {
    switch (form.view().scene) {
      case AccountScene::Out:
        return tr(STR_ACCOUNT_OUT_BODY);
      case AccountScene::In:
        return tr(STR_ACCOUNT_IN_BODY);
      case AccountScene::Limited:
        return tr(STR_ACCOUNT_LIMITED_BODY);
      case AccountScene::Manual:
        return tr(STR_ACCOUNT_MANUAL_BODY);
      case AccountScene::Busy:
        return tr(STR_ACCOUNT_BUSY_BODY);
      case AccountScene::Phone:
        return tr(STR_ACCOUNT_PHONE_BODY);
      case AccountScene::Wait:
        return tr(STR_ACCOUNT_WAIT_BODY);
      case AccountScene::Expired:
        return tr(STR_ACCOUNT_EXPIRED_BODY);
      case AccountScene::Failed:
        return tr(STR_ACCOUNT_FAILED_BODY);
      case AccountScene::TwoStep:
        return tr(STR_ACCOUNT_TWO_STEP_BODY);
      default:
        return tr(STR_ACCOUNT_OLD_BODY);
    }
  }
  const char* actionLabel(uint8_t index) const {
    switch (form.view().scene) {
      case AccountScene::Out:
        return index == 0 ? tr(STR_ACCOUNT_SIGN_IN) : tr(STR_ACCOUNT_USE_SAVED);
      case AccountScene::In:
        return index == 0 ? tr(STR_ACCOUNT_SIGN_IN_AGAIN) : tr(STR_BACK);
      case AccountScene::Limited:
      case AccountScene::Old:
        return tr(STR_BACK);
      case AccountScene::Busy:
        return tr(STR_CANCEL);
      case AccountScene::Manual:
        return index == 0 ? tr(STR_CANCEL) : tr(STR_ACCOUNT_SIGN_IN);
      case AccountScene::Phone:
        return index == 0 ? tr(STR_ACCOUNT_CHECK) : index == 1 ? tr(STR_ACCOUNT_TYPE_READER) : tr(STR_CANCEL);
      case AccountScene::Wait:
        return index == 0 ? tr(STR_ACCOUNT_CHECK) : tr(STR_CANCEL);
      case AccountScene::Expired:
        return index == 0 ? tr(STR_ACCOUNT_START_AGAIN) : tr(STR_CANCEL);
      case AccountScene::Failed:
        return index == 0 ? tr(STR_ACCOUNT_TRY_AGAIN) : tr(STR_CANCEL);
      default:
        return index == 0 ? tr(STR_ACCOUNT_FROM_PHONE) : tr(STR_CANCEL);
    }
  }
  // Bounded wrapping preserves the complete public link, including long path
  // segments. It never truncates a link into a different approval URL.
  int textLines(const char* text, int y, int maxLines, int font = UI_10_FONT_ID, int spacing = 4) const {
    const auto s = safe();
    const int line = renderer.getLineHeight(font) + spacing;
    char chunk[96]{};
    for (int row = 0; row < maxLines && *text; ++row) {
      unsigned n = 0, lastSpace = 0;
      while (text[n]) {
        const auto lead = static_cast<unsigned char>(text[n]);
        const unsigned bytes = lead < 0x80 ? 1 : (lead & 0xe0) == 0xc0 ? 2 : (lead & 0xf0) == 0xe0 ? 3 : 4;
        if (n + bytes >= sizeof(chunk)) break;
        unsigned copied = 0;
        while (copied < bytes && text[n + copied]) {
          chunk[n + copied] = text[n + copied];
          ++copied;
        }
        chunk[n + copied] = 0;
        if (renderer.getTextWidth(font, chunk) > s.width - 32) {
          chunk[n] = 0;
          break;
        }
        if (text[n] == ' ') lastSpace = n;
        n += copied;
      }
      if (!n) break;
      unsigned consumed = n;
      if (text[n] && lastSpace) {
        chunk[lastSpace] = 0;
        consumed = lastSpace + 1;
      }
      renderer.drawText(font, s.x + 16, y, chunk);
      text += consumed;
      while (*text == ' ') ++text;
      y += line;
    }
    return y;
  }
  void activateControl() {
    if (control < 2)
      form.chooseForm(control);
    else if (control < 5) {
      while (form.field() != control - 2) form.advanceFormField();
    } else if (const auto* key = keyAt(control - 5)) {
      if (key->kind == fui::KeyKind::Shift)
        shifted = !shifted;
      else if (key->kind == fui::KeyKind::Mode)
        symbols = !symbols;
      else if (key->value == fui::QWERTY_KEY_BACKSPACE)
        form.eraseForm();
      else if (key->value == fui::QWERTY_KEY_ENTER)
        form.advanceFormField();
      else if (key->output)
        for (const char* p = key->output; *p; ++p) form.appendForm(*p);
      else if (key->value == fui::QWERTY_KEY_SPACE)
        form.appendForm(' ');
      if (control >= keyCount() + 5) control = 5;
    }
  }

 public:
  AccountFormActivity(GfxRenderer& renderer, MappedInputManager& input, Library& library)
      : Activity("AccountForm", renderer, input), form(library) {}
  void onEnter() override {
    Activity::onEnter();
    form.enterForm();
    prepareQr();
    requestUpdate();
  }
  void onExit() override {
    form.retireForm();
    std::memset(qrBytes, 0, sizeof(qrBytes));
    Activity::onExit();
  }
  void loop() override {
    RenderLock lock;
    bool changed = form.pollForm();
    if (!form.live()) {
      activityManager.popActivity();
      return;
    }
    if (changed) control = 0;
    int x = 0, y = 0;
    const bool tapped = mappedInput.wasScreenTapped(x, y);
    const auto s = safe();
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || (tapped && inside(Rect{s.x, s.y, 100, 70}, x, y)))
      form.retireForm();
    else if (form.view().scene == AccountScene::Manual) {
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        if (control + 1 < 5 + keyCount()) ++control;
        changed = true;
      } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        if (control) --control;
        changed = true;
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        activateControl();
        changed = true;
      } else if (tapped) {
        for (uint8_t i = 0; i < 2; ++i)
          if (inside(actionBounds(i), x, y)) {
            control = i;
            activateControl();
            changed = true;
          }
        for (uint8_t i = 0; i < 3; ++i)
          if (inside(fieldBounds(i), x, y)) {
            control = i + 2;
            activateControl();
            changed = true;
          }
        for (uint8_t i = 0; i < keyCount(); ++i)
          if (inside(keyBounds(i), x, y)) {
            control = i + 5;
            activateControl();
            changed = true;
            break;
          }
      }
    } else {
      if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Down))) {
        form.stepForm(1);
        changed = true;
      } else if (mappedInput.wasReleased(mappedInput.menuButton(MappedInputManager::Button::Up))) {
        form.stepForm(-1);
        changed = true;
      } else if (mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
        form.activateForm();
        changed = true;
      } else if (tapped)
        for (uint8_t i = 0; i < form.actions(); ++i)
          if (inside(actionBounds(i), x, y)) {
            form.chooseForm(i);
            changed = true;
            break;
          }
    }
    prepareQr();
    if (!form.live())
      activityManager.popActivity();
    else if (changed)
      requestUpdate();
  }
  void render(RenderLock&&) override {
    if (!form.live()) return;
#ifdef SIMULATOR
    ++formPaints;
#endif
    renderer.clearScreen();
    const auto s = safe();
    renderer.drawText(UI_10_FONT_ID, s.x + 16, s.y + 8, tr(STR_BACK));
    renderer.drawText(BITTER_14_FONT_ID, s.x + 16, formContentTop(), title());
    textLines(body(), formContentTop() + 44, 3);
    const auto scene = form.view().scene;
    if (scene == AccountScene::Out || scene == AccountScene::In || scene == AccountScene::Limited) {
      const auto& a = form.view().account;
      const char* labels[] = {tr(STR_ACCOUNT_ACCOUNT), tr(STR_ACCOUNT_SYNC), tr(STR_ACCOUNT_CATALOG)};
      const bool supported[] = {a.accountSupported, a.syncSupported, a.catalogSupported};
      const bool ready[] = {a.signedIn, a.syncReady, a.catalogReady};
      for (uint8_t i = 0; i < 3; ++i) {
        const auto r = fieldBounds(i);
        renderer.drawText(UI_10_FONT_ID, r.x, r.y, labels[i]);
        renderer.drawText(UI_10_FONT_ID, r.x + r.width / 2, r.y,
                          !supported[i] ? tr(STR_ACCOUNT_NOT_SUPPORTED)
                          : ready[i]    ? (i == 0 ? tr(STR_ACCOUNT_SIGNED_IN) : tr(STR_ACCOUNT_AVAILABLE))
                                        : (i == 0 ? tr(STR_ACCOUNT_NOT_SIGNED_IN) : tr(STR_ACCOUNT_NOT_SET)));
      }
    }
    if (scene == AccountScene::Manual) {
      const char* labels[] = {tr(STR_ACCOUNT_SERVER), tr(STR_ACCOUNT_USER), tr(STR_ACCOUNT_PASSWORD)};
      const char* values[] = {form.entry().server.data(), form.entry().user.data(), tr(STR_ACCOUNT_MASKED)};
      for (uint8_t i = 0; i < 3; ++i) {
        const auto r = fieldBounds(i);
        renderer.drawRect(r.x, r.y, r.width, r.height, form.field() == i ? 3 : 1, true);
        renderer.drawText(UI_10_FONT_ID, r.x + 8, r.y + 7, labels[i]);
        const int valueX = r.x + r.width * 2 / 5;
        renderer.beginTextClip(valueX, r.y + 4, r.width * 3 / 5 - 8, r.height - 8);
        renderer.drawText(UI_10_FONT_ID, valueX, r.y + 7, values[i][0] ? values[i] : tr(STR_ACCOUNT_NOT_ENTERED));
        renderer.endTextClip();
      }
      for (uint8_t i = 0; i < keyCount(); ++i) {
        const auto r = keyBounds(i);
        const auto* key = keyAt(i);
        renderer.drawRect(r.x, r.y, r.width, r.height, control == i + 5 ? 3 : 1, true);
        const char* label = key->label;
        if (!label)
          label = key->kind == fui::KeyKind::Shift          ? tr(STR_ACCOUNT_SHIFT)
                  : key->value == fui::QWERTY_KEY_BACKSPACE ? tr(STR_ACCOUNT_DELETE)
                  : key->value == fui::QWERTY_KEY_ENTER     ? tr(STR_ACCOUNT_NEXT_FIELD)
                                                            : tr(STR_ACCOUNT_SPACE);
        renderer.drawText(SMALL_FONT_ID, r.x + 4, r.y + 7, label);
      }
    }
    if ((scene == AccountScene::Phone || scene == AccountScene::Wait) && form.view().link[0]) {
      const bool longLink = std::strlen(form.view().link.data()) > 128;
      const int qrY = formContentTop() + (longLink ? 132 : 148);
      const int scale = longLink ? 2 : 3;
      const int qrX = s.x + (s.width - (qr.size + 8) * scale) / 2 + 4 * scale;
      if (qrReady) {
        for (uint8_t y = 0; y < qr.size; ++y)
          for (uint8_t x = 0; x < qr.size; ++x)
            if (qrcode_getModule(&qr, x, y)) renderer.fillRect(qrX + x * scale, qrY + y * scale, scale, scale, true);
      }
      const int linkY = qrReady ? qrY + (qr.size + 4) * scale : qrY;
      const int linkFont = longLink ? SMALL_FONT_ID : UI_10_FONT_ID;
      const int spacing = longLink ? 0 : 4;
      const int linkLines = (actionBounds(0).y - linkY - 8) / (renderer.getLineHeight(linkFont) + spacing);
      textLines(form.view().link.data(), linkY, linkLines, linkFont, spacing);
    }
    for (uint8_t i = 0; i < form.actions(); ++i) {
      const auto r = actionBounds(i);
      const bool focused = scene == AccountScene::Manual ? control == i : form.focus() == i;
      renderer.drawRect(r.x, r.y, r.width, r.height, focused ? 3 : 1, true);
      renderer.drawText(UI_10_FONT_ID, r.x + 12, r.y + 10, actionLabel(i));
    }
    const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_HOME_PREV), tr(STR_HOME_NEXT));
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
    renderer.displayBuffer(HalDisplay::FAST_REFRESH);
  }
};
static_assert(sizeof(AccountFormActivity) <= 4096);
}  // namespace
void showAccountForm(GfxRenderer& renderer, MappedInputManager& input, Library& library) {
  // <=4 KiB foreground activity holds the entry, cached view and QR workspace;
  // too large for a task stack, freed by ActivityManager on exit. No extra framebuffer.
  auto screen = makeUniqueNoThrow<AccountFormActivity>(renderer, input, library);
  if (!screen) {
    LOG_ERR("BSA", "Cannot allocate account screen");
    return;
  }
  activityManager.replaceActivity(std::move(screen));
}
#ifdef SIMULATOR
uint32_t accountFormPaints() {
  RenderLock lock;
  return formPaints;
}
#endif
}  // namespace booksync
