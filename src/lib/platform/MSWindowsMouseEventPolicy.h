/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/MSWindowsHook.h"

namespace deskflow::platform {

enum class PreModeMouseEventAction
{
  Process,
  Suppress,
  PassThrough
};

inline bool isMouseButtonMessage(WPARAM message)
{
  switch (message) {
  case WM_LBUTTONDOWN:
  case WM_LBUTTONUP:
  case WM_LBUTTONDBLCLK:
  case WM_MBUTTONDOWN:
  case WM_MBUTTONUP:
  case WM_MBUTTONDBLCLK:
  case WM_RBUTTONDOWN:
  case WM_RBUTTONUP:
  case WM_RBUTTONDBLCLK:
  case WM_XBUTTONDOWN:
  case WM_XBUTTONUP:
  case WM_XBUTTONDBLCLK:
  case WM_NCLBUTTONDOWN:
  case WM_NCLBUTTONUP:
  case WM_NCLBUTTONDBLCLK:
  case WM_NCMBUTTONDOWN:
  case WM_NCMBUTTONUP:
  case WM_NCMBUTTONDBLCLK:
  case WM_NCRBUTTONDOWN:
  case WM_NCRBUTTONUP:
  case WM_NCRBUTTONDBLCLK:
  case WM_NCXBUTTONDOWN:
  case WM_NCXBUTTONUP:
  case WM_NCXBUTTONDBLCLK:
    return true;

  default:
    return false;
  }
}

inline PreModeMouseEventAction classifyPreModeMouseEvent(
    EHookMode mode, WPARAM message, DWORD eventTime, DWORD modeCutoff, bool hasModeCutoff,
    PreModeMouseEventAction staleButtonAction
)
{
  const bool handlesMouseInput = mode == kHOOK_WATCH_JUMP_ZONE || mode == kHOOK_RELAY_EVENTS;
  const bool isHandledMessage = message == WM_MOUSEMOVE || isMouseButtonMessage(message);
  if (!handlesMouseInput || !isHandledMessage || !hasModeCutoff) {
    return PreModeMouseEventAction::Process;
  }

  // Event timestamps and GetTickCount share the Win32 millisecond tick domain.
  // Signed subtraction keeps the comparison valid when the 32-bit counter wraps.
  if (static_cast<LONG>(eventTime - modeCutoff) > 0) {
    return PreModeMouseEventAction::Process;
  }

  // Motion from an earlier mode must not move the newly active screen. Button
  // handling depends on whether the previous mode owned the local OS input.
  // In either case it must bypass Deskflow so an old event cannot rebuild the
  // new epoch's shadow state.
  return message == WM_MOUSEMOVE ? PreModeMouseEventAction::Suppress : staleButtonAction;
}

} // namespace deskflow::platform
