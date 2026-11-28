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
#include "components/BookSyncPresentation.h"
#include "components/UITheme.h"
#include "fontIds.h"
namespace booksync {
namespace {
namespace fui = freeink::ui;
namespace kit = BookSyncPresentation;
#ifdef SIMULATOR
uint32_t formPaints = 0;
Rect formTargets[64];
#endif
class AccountFormActivity final : public Activity {
  AccountForm form;
  // One foreground-lifetime workspace; no allocation or QR encoding in paint.
  uint8_t qrBytes[512]{};
  QRCode qr{};
  uint64_t qrRevision = 0;
  bool qrReady = false, shifted = false, symbols = false;
  uint8_t control = 0;
  kit::Frame presentation() const { return kit::frame(renderer, mappedInput.hasTouch(), gpio.deviceIsX3()); }
  int formContentTop() const {
    const auto f = presentation();
    return f.content().y + f.unit(26);
  }
  int bodyTop() const {
    const auto f = presentation();
    return formContentTop() + kit::paragraphHeight(renderer, f, f.content().width, title(), kit::Type::Title) +
           f.unit(16);
  }
  int detailsTop() const {
    const auto f = presentation();
    return bodyTop() + kit::paragraphHeight(renderer, f, f.content().width, body()) + f.unit(22);
  }
  Rect keyboardBounds() const {
    const auto f = presentation();
    return Rect{f.content().x, f.footer().y - f.unit(144), f.content().width, f.unit(132)};
  }
  Rect fieldBounds(uint8_t index) const {
    const auto f = presentation();
    return f.row(detailsTop() + index * f.unit(62));
  }
  int actionTop() const {
    const auto f = presentation();
    const auto scene = form.view().scene;
    int y = detailsTop();
    if (scene == AccountScene::Out || scene == AccountScene::In || scene == AccountScene::Limited ||
        scene == AccountScene::Manual)
      y += f.unit(186 + 22);
    if (scene == AccountScene::Phone && form.view().link[0]) y = linkTop() + linkHeight() + f.unit(22);
    return y;
  }
  int qrScale() const { return std::strlen(form.view().link.data()) > 128 ? 1 : 3; }
  int qrTop() const { return detailsTop(); }
  int linkTop() const { return qrTop() + (qrReady ? (qr.size + 8) * qrScale() : 0) + presentation().unit(16); }
  int linkHeight() const {
    const auto f = presentation();
    return kit::paragraphHeight(renderer, f, f.content().width, form.view().link.data(), kit::Type::Caption);
  }
  TouchActionButtons::Layout actionLayout() const { return presentation().actions(actionTop(), form.actions()); }
  Rect actionBounds(uint8_t index) const { return actionLayout().buttons[index]; }
  const fui::KeyboardLayout& formKeyboardLayout() const {
    return fui::builtinKeyboardLayout(fui::KeyboardLayoutId::QwertyEn, shifted, symbols, false);
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
    if ((form.view().scene == AccountScene::Phone) && form.view().link[0]) {
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
#ifdef SIMULATOR
    for (auto& target : formTargets) target = Rect{};
#endif
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
    if (mappedInput.wasReleased(MappedInputManager::Button::Back) || (tapped && inside(presentation().back(), x, y)))
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
    for (auto& target : formTargets) target = Rect{};
    for (uint8_t i = 0; i < form.actions(); ++i) formTargets[i] = actionBounds(i);
    if (form.view().scene == AccountScene::Manual) {
      for (uint8_t i = 0; i < 3; ++i) formTargets[i + 2] = fieldBounds(i);
      for (uint8_t i = 0; i < keyCount() && i + 5 < 64; ++i) formTargets[i + 5] = keyBounds(i);
    }
#endif
    renderer.clearScreen();
    const auto f = presentation();
    const auto c = f.content();
    kit::header(renderer, f, tr(STR_ACCOUNT_YOUR_SERVER));
    kit::paragraph(renderer, f, Rect{c.x, formContentTop(), c.width, f.unit(80)}, title(), kit::Type::Title);
    kit::paragraph(renderer, f, Rect{c.x, bodyTop(), c.width, f.unit(120)}, body());
    const auto scene = form.view().scene;
    if (scene == AccountScene::Out || scene == AccountScene::In || scene == AccountScene::Limited) {
      const auto& a = form.view().account;
      const char* labels[] = {tr(STR_ACCOUNT_ACCOUNT), tr(STR_ACCOUNT_SYNC), tr(STR_ACCOUNT_CATALOG)};
      const bool supported[] = {a.accountSupported, a.syncSupported, a.catalogSupported};
      const bool ready[] = {a.signedIn, a.syncReady, a.catalogReady};
      for (uint8_t i = 0; i < 3; ++i) {
        const auto r = fieldBounds(i);
        kit::row(renderer, f, r, labels[i],
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
        kit::row(renderer, f, r, labels[i], values[i][0] ? values[i] : tr(STR_ACCOUNT_NOT_ENTERED), control == i + 2,
                 false, control >= 5 && form.field() == i);
      }
      for (uint8_t i = 0; i < keyCount(); ++i) {
        const auto r = keyBounds(i);
        const auto* key = keyAt(i);
        renderer.drawRoundedRect(r.x, r.y, r.width, r.height, control == i + 5 ? 2 : 1, f.unit(4), true);
        const char* label = key->label;
        if (!label)
          label = key->kind == fui::KeyKind::Shift          ? tr(STR_ACCOUNT_SHIFT)
                  : key->value == fui::QWERTY_KEY_BACKSPACE ? tr(STR_ACCOUNT_DELETE)
                  : key->value == fui::QWERTY_KEY_ENTER     ? tr(STR_ACCOUNT_NEXT_FIELD)
                                                            : tr(STR_ACCOUNT_SPACE);
        kit::text(renderer, f, Rect{r.x + 3, r.y + 2, r.width - 6, r.height - 4}, label, kit::Type::Body, true);
      }
    }
    if (scene == AccountScene::Phone && form.view().link[0]) {
      const int scale = qrScale(), qrY = qrTop();
      const int qrX = f.area.x + (f.area.width - qr.size * scale) / 2;
      if (qrReady) {
        for (uint8_t y = 0; y < qr.size; ++y)
          for (uint8_t x = 0; x < qr.size; ++x)
            if (qrcode_getModule(&qr, x, y)) renderer.fillRect(qrX + x * scale, qrY + y * scale, scale, scale, true);
      }
      // Full live link is wrapped, never ellipsized into a different URL.
      kit::paragraph(renderer, f, Rect{c.x, linkTop(), c.width, linkHeight()}, form.view().link.data(),
                     kit::Type::Caption);
    }
    const char* labels[3]{};
    for (uint8_t i = 0; i < form.actions(); ++i) labels[i] = actionLabel(i);
    kit::drawActions(renderer, f, actionLayout(), labels, 0,
                     scene == AccountScene::Manual ? control < 2 ? control : -1 : form.focus());
    kit::footer(renderer, f, title());
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
bool accountFormControlCenter(uint8_t index, int& x, int& y) {
  RenderLock lock;
  if (index >= 64 || formTargets[index].width <= 0) return false;
  const auto r = formTargets[index];
  x = r.x + r.width / 2;
  y = r.y + r.height / 2;
  return true;
}
uint32_t accountFormPaints() {
  RenderLock lock;
  return formPaints;
}
#endif
}  // namespace booksync
