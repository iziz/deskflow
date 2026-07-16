/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsHook.h"

namespace deskflow::platform {

inline bool shouldDropPreRelayMouseMotion(
    EHookMode mode, WPARAM message, DWORD eventTime, DWORD relayCutoff, bool hasRelayCutoff
)
{
  if (mode != kHOOK_RELAY_EVENTS || message != WM_MOUSEMOVE || !hasRelayCutoff) {
    return false;
  }

  // Event timestamps and GetTickCount share the Win32 millisecond tick domain.
  // Signed subtraction keeps the comparison valid when the 32-bit counter wraps.
  return static_cast<LONG>(eventTime - relayCutoff) <= 0;
}

} // namespace deskflow::platform
