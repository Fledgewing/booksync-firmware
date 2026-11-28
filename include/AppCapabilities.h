#pragma once

// The app deliberately has a build-time touch capability, separate from a
// runtime probe. Button-only images must not retain the touch UI merely because
// the shared source tree also builds for Sticky.
#ifndef CROSSINK_APP_CAP_TOUCH
#error "Define CROSSINK_APP_CAP_TOUCH as 0 or 1 in the PlatformIO environment"
#endif

#if CROSSINK_APP_CAP_TOUCH != 0 && CROSSINK_APP_CAP_TOUCH != 1
#error "CROSSINK_APP_CAP_TOUCH must be 0 or 1"
#endif

#ifndef CROSSINK_APP_CAP_USB_DRIVE
#error "Define CROSSINK_APP_CAP_USB_DRIVE as 0 or 1 in the PlatformIO environment"
#endif

#if CROSSINK_APP_CAP_USB_DRIVE != 0 && CROSSINK_APP_CAP_USB_DRIVE != 1
#error "CROSSINK_APP_CAP_USB_DRIVE must be 0 or 1"
#endif

// Native simulator BoardConfig intentionally exposes only simulated runtime
// profiles, so keep this firmware-image identity available at the app layer.
#if defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC
#define CROSSINK_APP_DEVICE_X4CLASSIC 1
#else
#define CROSSINK_APP_DEVICE_X4CLASSIC 0
#endif

// Native simulator BoardConfig deliberately has no FREEINK_CAP_TOUCH macro.
// Firmware builds must keep the app and SDK capability selections in lockstep.
#if !defined(SIMULATOR)
#include <BoardConfig.h>
#if CROSSINK_APP_CAP_TOUCH != FREEINK_CAP_TOUCH
#error "CROSSINK_APP_CAP_TOUCH must match FREEINK_CAP_TOUCH"
#endif
#if CROSSINK_APP_CAP_USB_DRIVE != FREEINK_CAP_USB_MSC
#error "CROSSINK_APP_CAP_USB_DRIVE must match FREEINK_CAP_USB_MSC"
#endif
#endif

// Every button-only reader uses the full-screen menu and its bounded sample preview.
#define CROSSINK_APP_READER_SAMPLE_PREVIEW (!CROSSINK_APP_CAP_TOUCH)

// Approved spatial X4 and Settings-list X3 editor presentations.
#if (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) ||                           \
    (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC) ||                   \
    (defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3) || defined(SIMULATOR_DEVICE_X3) || \
    defined(SIMULATOR_DEVICE_X4_PRO) || defined(SIMULATOR_DEVICE_X4_CLASSIC)
#define CROSSINK_APP_CAP_HOME_EDITOR 1
#else
#define CROSSINK_APP_CAP_HOME_EDITOR 0
#endif

// Only X3/C3 uses the Settings-list editor entry. X4 has its spatial editor.
#if (defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3) || defined(SIMULATOR_DEVICE_X3)
#define CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS 1
#else
#define CROSSINK_APP_CAP_HOME_EDITOR_SETTINGS 0
#endif

// R1 reminders are approved for these BookSync reader profiles only. Keep this
// policy separate from touch, RTC availability and PSRAM capabilities.
#if (defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3) || (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) || \
    (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC) || defined(SIMULATOR_DEVICE_X3) ||              \
    defined(SIMULATOR_DEVICE_X4_PRO) || defined(SIMULATOR_DEVICE_X4_CLASSIC)
#define CROSSINK_APP_CAP_REMINDERS 1
#else
#define CROSSINK_APP_CAP_REMINDERS 0
#endif

#if (defined(FREEINK_DEVICE_X3) && FREEINK_DEVICE_X3) || defined(SIMULATOR_DEVICE_X3)
#define CROSSINK_APP_REMINDER_TEXT_LAYOUT 1
#else
#define CROSSINK_APP_REMINDER_TEXT_LAYOUT 0
#endif

// Approved X4 Home measurement source; C3/X3 and unrelated profiles do not
// retain this producer or its static workspace.
#if (defined(FREEINK_DEVICE_X4PRO) && FREEINK_DEVICE_X4PRO) ||                                             \
    (defined(FREEINK_DEVICE_X4CLASSIC) && FREEINK_DEVICE_X4CLASSIC) || defined(SIMULATOR_DEVICE_X4_PRO) || \
    defined(SIMULATOR_DEVICE_X4_CLASSIC)
#define CROSSINK_APP_CAP_BATTERY_HISTORY 1
#else
#define CROSSINK_APP_CAP_BATTERY_HISTORY 0
#endif
