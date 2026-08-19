/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/IPrimaryScreen.h"

#include <optional>

namespace deskflow::server {

inline std::optional<bool> lockStateForNativeScrollLock(
    IPrimaryScreen::MotionInfo::ScrollLockState nativeState, bool polarityInverted
)
{
  using enum IPrimaryScreen::MotionInfo::ScrollLockState;

  if (nativeState == Unsupported) {
    return std::nullopt;
  }

  const bool nativeEnabled = nativeState == On;
  return nativeEnabled != polarityInverted;
}

} // namespace deskflow::server
