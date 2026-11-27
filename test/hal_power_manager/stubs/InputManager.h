#pragma once
#include <cstdint>
class InputManager {
 public:
  bool isPowerButtonPhysicallyPressed() const { return false; }
  static constexpr uint8_t MAX_TOUCH_CONTACTS = 5;
};
