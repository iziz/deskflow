/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"
#include "platform/MSWindowsHook.h"

namespace deskflow::platform {

inline constexpr KeyModifierMask kWindowsToggleModifierMask =
    KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;

enum class WindowsHotKeyRoute
{
  ModifierOnly,
  VirtualKey
};

inline bool isWindowsToggleKey(UINT virtualKey)
{
  switch (virtualKey) {
  case VK_CAPITAL:
  case VK_NUMLOCK:
  case VK_SCROLL:
    return true;

  default:
    return false;
  }
}

inline bool shouldAdvanceWindowsToggleKeyState(UINT virtualKey, bool keyDown, bool wasDown)
{
  return keyDown && !wasDown && isWindowsToggleKey(virtualKey);
}

inline KeyModifierMask modifierForWindowsToggleKey(UINT virtualKey)
{
  switch (virtualKey) {
  case VK_CAPITAL:
    return KeyModifierCapsLock;

  case VK_NUMLOCK:
    return KeyModifierNumLock;

  case VK_SCROLL:
    return KeyModifierScrollLock;

  default:
    return 0;
  }
}

inline KeyModifierMask advanceWindowsToggleKeyState(
    KeyModifierMask modifiers, UINT virtualKey, bool keyDown, bool wasDown
)
{
  if (shouldAdvanceWindowsToggleKeyState(virtualKey, keyDown, wasDown)) {
    modifiers ^= modifierForWindowsToggleKey(virtualKey);
  }
  return modifiers;
}

inline KeyModifierMask synchronizeWindowsToggleKeyState(KeyModifierMask inputDesktopModifiers)
{
  return inputDesktopModifiers & kWindowsToggleModifierMask;
}

inline WindowsHotKeyRoute windowsHotKeyRoute(UINT virtualKey)
{
  switch (virtualKey) {
  case VK_SHIFT:
  case VK_LSHIFT:
  case VK_RSHIFT:
  case VK_CONTROL:
  case VK_LCONTROL:
  case VK_RCONTROL:
  case VK_MENU:
  case VK_LMENU:
  case VK_RMENU:
  case VK_LWIN:
  case VK_RWIN:
    return WindowsHotKeyRoute::ModifierOnly;

  default:
    return WindowsHotKeyRoute::VirtualKey;
  }
}

inline bool shouldRegisterHotKeyWithWindows(UINT virtualKey, UINT modifiers)
{
  if (modifiers != 0) {
    return true;
  }

  if (isWindowsToggleKey(virtualKey)) {
    // Keep unmodified toggle keys in the regular keyboard path so Windows
    // updates its toggle state and keyboard LEDs before Deskflow handles them.
    return false;
  }

  return true;
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
