/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "platform/OSXAutoTypes.h"

#include <Carbon/Carbon.h>

#include <string>

namespace OSXInputSources {

struct CurrentKeyboardInputSources
{
  std::string inputSourceId;
  std::string keyboardLayoutId;
};

std::string inputSourceId(TISInputSourceRef source);
CurrentKeyboardInputSources currentKeyboardInputSources();

AutoCFData copyUnicodeKeyLayoutData(TISInputSourceRef source);
AutoCFData copyCurrentKeyboardLayoutData();
AutoCFData copyAsciiCapableKeyLayoutData();
AutoCFArray copyKeyboardInputSources();

bool currentKeyboardInputSourceCanBeEnabled();
OSStatus selectInputSource(TISInputSourceRef source);

} // namespace OSXInputSources
