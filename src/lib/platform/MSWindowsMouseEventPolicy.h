/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsHook.h"

namespace deskflow::platform {

inline bool shouldDropPreModeMouseMotion(
    EHookMode mode, WPARAM message, DWORD eventTime, DWORD modeCutoff, bool hasModeCutoff
)
{
  const bool handlesMouseMotion = mode == kHOOK_WATCH_JUMP_ZONE || mode == kHOOK_RELAY_EVENTS;
  if (!handlesMouseMotion || message != WM_MOUSEMOVE || !hasModeCutoff) {
    return false;
  }

  // Event timestamps and GetTickCount share the Win32 millisecond tick domain.
  // Signed subtraction keeps the comparison valid when the 32-bit counter wraps.
  return static_cast<LONG>(eventTime - modeCutoff) <= 0;
}

} // namespace deskflow::platform
