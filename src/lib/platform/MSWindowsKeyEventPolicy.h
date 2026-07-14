/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsHook.h"

namespace deskflow::platform {

inline bool shouldRegisterHotKeyWithWindows(UINT virtualKey, UINT modifiers)
{
  if (modifiers != 0) {
    return true;
  }

  switch (virtualKey) {
  case VK_CAPITAL:
  case VK_NUMLOCK:
  case VK_SCROLL:
    // Keep unmodified toggle keys in the regular keyboard path so Windows
    // updates its toggle state and keyboard LEDs before Deskflow handles them.
    return false;

  default:
    return true;
  }
}

inline bool shouldSuppressLocalKey(EHookMode mode, WPARAM virtualKey, LPARAM keyInfo, bool lowLevelHookActive)
{
  if (mode != kHOOK_RELAY_EVENTS) {
    return false;
  }

  switch (virtualKey) {
  case VK_CAPITAL:
  case VK_NUMLOCK:
  case VK_SCROLL:
    // Let toggle keys reach Windows so the keyboard lights remain synchronized.
    return false;

  case VK_HANGUL: {
    constexpr uint32_t spaceScanCode = 0x39u;
    const auto scanCode = static_cast<uint32_t>((static_cast<uintptr_t>(keyInfo) >> 16u) & 0x1ffu);

    // Korean IME rewrites Shift+Space as a non-extended Hangul event with the
    // Space scan code. Passing it through produces a stray Space event in the
    // local foreground application while input is relayed to another screen.
    if (scanCode == spaceScanCode) {
      return true;
    }

    // Preserve the existing local handling for a physical Hangul key when a
    // low-level hook is active.
    return !lowLevelHookActive;
  }

  default:
    return true;
  }
}

} // namespace deskflow::platform
