/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#define WIN32_LEAN_AND_MEAN
#include <Windows.h>

inline constexpr ULONG_PTR kWindowsLocalKeyRestoreExtraInfo = static_cast<ULONG_PTR>(0x44534652u);

inline bool isWindowsLocalKeyRestoreInput(DWORD hookFlags, ULONG_PTR extraInfo)
{
  return (hookFlags & LLKHF_INJECTED) != 0 && extraInfo == kWindowsLocalKeyRestoreExtraInfo;
}

inline INPUT makeWindowsKeyInput(UINT virtualKey, KeyButton button, bool press, ULONG_PTR extraInfo = 0)
{
  INPUT input = {};
  input.type = INPUT_KEYBOARD;
  input.ki.wVk = static_cast<WORD>(virtualKey);
  input.ki.dwExtraInfo = extraInfo;
  if ((button & 0x100u) != 0) {
    input.ki.dwFlags |= KEYEVENTF_EXTENDEDKEY;
  }
  if (!press) {
    input.ki.dwFlags |= KEYEVENTF_KEYUP;
  }
  return input;
}
