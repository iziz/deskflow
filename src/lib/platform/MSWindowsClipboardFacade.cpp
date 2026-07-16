/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/MSWindowsClipboardFacade.h"

#include "base/Log.h"

bool MSWindowsClipboardFacade::write(HANDLE win32Data, UINT win32Format)
{
  if (SetClipboardData(win32Format, win32Data) == nullptr) {
    const auto error = GetLastError();
    // free converted data if we couldn't put it on
    // the clipboard.
    GlobalFree(win32Data);
    LOG_WARN("failed to set Windows clipboard data: format=%u error=%lu", win32Format, error);
    return false;
  }

  return true;
}
