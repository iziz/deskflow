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

enum WindowsNativeKeyState : uint32_t
{
  WindowsNativeKeyStateLeftShift = 1u << 0u,
  WindowsNativeKeyStateRightShift = 1u << 1u,
  WindowsNativeKeyStateLeftControl = 1u << 2u,
  WindowsNativeKeyStateRightControl = 1u << 3u,
  WindowsNativeKeyStateLeftAlt = 1u << 4u,
  WindowsNativeKeyStateRightAlt = 1u << 5u,
  WindowsNativeKeyStateLeftSuper = 1u << 6u,
  WindowsNativeKeyStateRightSuper = 1u << 7u,
  WindowsNativeKeyStateCapsLock = 1u << 8u,
  WindowsNativeKeyStateNumLock = 1u << 9u,
  WindowsNativeKeyStateScrollLock = 1u << 10u
};

inline uint32_t windowsNativeKeyDownFlag(UINT virtualKey)
{
  switch (virtualKey) {
  case VK_LSHIFT:
    return WindowsNativeKeyStateLeftShift;
  case VK_RSHIFT:
    return WindowsNativeKeyStateRightShift;
  case VK_LCONTROL:
    return WindowsNativeKeyStateLeftControl;
  case VK_RCONTROL:
    return WindowsNativeKeyStateRightControl;
  case VK_LMENU:
    return WindowsNativeKeyStateLeftAlt;
  case VK_RMENU:
    return WindowsNativeKeyStateRightAlt;
  case VK_LWIN:
    return WindowsNativeKeyStateLeftSuper;
  case VK_RWIN:
    return WindowsNativeKeyStateRightSuper;
  default:
    return 0;
  }
}

inline uint32_t advanceWindowsHookHeldKeyState(uint32_t nativeState, UINT virtualKey, bool keyDown)
{
  const auto flag = windowsNativeKeyDownFlag(virtualKey);
  if (keyDown) {
    return nativeState | flag;
  }

  return nativeState & ~flag;
}

inline KeyModifierMask windowsModifierMaskFromNativeKeyState(uint32_t nativeState)
{
  KeyModifierMask modifiers = 0;
  if ((nativeState & (WindowsNativeKeyStateLeftShift | WindowsNativeKeyStateRightShift)) != 0u) {
    modifiers |= KeyModifierShift;
  }
  if ((nativeState & (WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl)) != 0u) {
    modifiers |= KeyModifierControl;
  }
  if ((nativeState & (WindowsNativeKeyStateLeftAlt | WindowsNativeKeyStateRightAlt)) != 0u) {
    modifiers |= KeyModifierAlt;
  }
  if ((nativeState & (WindowsNativeKeyStateLeftSuper | WindowsNativeKeyStateRightSuper)) != 0u) {
    modifiers |= KeyModifierSuper;
  }
  if ((nativeState & WindowsNativeKeyStateCapsLock) != 0u) {
    modifiers |= KeyModifierCapsLock;
  }
  if ((nativeState & WindowsNativeKeyStateNumLock) != 0u) {
    modifiers |= KeyModifierNumLock;
  }
  if ((nativeState & WindowsNativeKeyStateScrollLock) != 0u) {
    modifiers |= KeyModifierScrollLock;
  }
  return modifiers;
}

enum class WindowsHotKeyRoute
{
  ModifierOnly,
  VirtualKey
};

enum class WindowsPrimaryKeyRestoreRoute
{
  Relay,
  ForgetStaleRestore,
  RestoreLocally
};

inline WindowsPrimaryKeyRestoreRoute windowsPrimaryKeyRestoreRoute(
    bool isOnScreen, bool keyDown, bool wasDown, bool trackedForLocalRestore
)
{
  if (isOnScreen || !trackedForLocalRestore) {
    return WindowsPrimaryKeyRestoreRoute::Relay;
  }

  if (!keyDown) {
    return WindowsPrimaryKeyRestoreRoute::RestoreLocally;
  }

  // A fresh press cannot belong to the key that was held before the screen
  // switch. Drop that stale restore marker so the matching release is relayed
  // to the remote screen.
  if (!wasDown) {
    return WindowsPrimaryKeyRestoreRoute::ForgetStaleRestore;
  }

  return WindowsPrimaryKeyRestoreRoute::Relay;
}

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

inline KeyModifierMask reconcileWindowsToggleKeyState(
    KeyModifierMask modifiers, UINT virtualKey, bool enabled
)
{
  const auto modifier = modifierForWindowsToggleKey(virtualKey);
  if (enabled) {
    return modifiers | modifier;
  }

  return modifiers & ~modifier;
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
