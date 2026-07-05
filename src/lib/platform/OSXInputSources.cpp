/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXInputSources.h"

#include "base/Log.h"

#include <vector>

namespace {

std::string cfStringToUtf8(CFStringRef string)
{
  if (!string) {
    return {};
  }

  if (const auto direct = CFStringGetCStringPtr(string, kCFStringEncodingUTF8)) {
    return direct;
  }

  const auto length = CFStringGetLength(string);
  const auto maxSize = CFStringGetMaximumSizeForEncoding(length, kCFStringEncodingUTF8) + 1;
  std::vector<char> buffer(static_cast<size_t>(maxSize));

  if (!CFStringGetCString(string, buffer.data(), maxSize, kCFStringEncodingUTF8)) {
    return {};
  }

  return buffer.data();
}

std::string inputSourceIdOnTISThread(TISInputSourceRef source)
{
  if (!source) {
    return {};
  }

  return cfStringToUtf8((CFStringRef)TISGetInputSourceProperty(source, kTISPropertyInputSourceID));
}

AutoCFData copyUnicodeKeyLayoutDataOnTISThread(TISInputSourceRef source)
{
  if (!source) {
    return AutoCFData(nullptr, CFRelease);
  }

  const auto resourceRef = (CFDataRef)TISGetInputSourceProperty(source, kTISPropertyUnicodeKeyLayoutData);
  return AutoCFData(resourceRef != nullptr ? CFDataCreateCopy(nullptr, resourceRef) : nullptr, CFRelease);
}

} // namespace

namespace OSXInputSources {

std::string inputSourceId(TISInputSourceRef source)
{
  return runTISOnMainThread([source] { return inputSourceIdOnTISThread(source); });
}

CurrentKeyboardInputSources currentKeyboardInputSources()
{
  return runTISOnMainThread([] {
    AutoTISInputSourceRef inputSource(TISCopyCurrentKeyboardInputSource(), CFRelease);
    AutoTISInputSourceRef keyboardLayout(TISCopyCurrentKeyboardLayoutInputSource(), CFRelease);
    return CurrentKeyboardInputSources{
        inputSourceIdOnTISThread(inputSource.get()),
        inputSourceIdOnTISThread(keyboardLayout.get()),
    };
  });
}

AutoCFData copyUnicodeKeyLayoutData(TISInputSourceRef source)
{
  return runTISOnMainThread([source] { return copyUnicodeKeyLayoutDataOnTISThread(source); });
}

AutoCFData copyCurrentKeyboardLayoutData()
{
  return runTISOnMainThread([] {
    AutoTISInputSourceRef currentKeyboardLayout(TISCopyCurrentKeyboardLayoutInputSource(), CFRelease);
    return copyUnicodeKeyLayoutDataOnTISThread(currentKeyboardLayout.get());
  });
}

AutoCFData copyAsciiCapableKeyLayoutData()
{
  return runTISOnMainThread([] {
    AutoTISInputSourceRef keyboardLayout(TISCopyCurrentASCIICapableKeyboardLayoutInputSource(), CFRelease);
    if (!keyboardLayout) {
      LOG_WARN("can't get the ASCII-capable keyboard layout for IME fallback");
      return AutoCFData(nullptr, CFRelease);
    }

    LOG_VERBOSE(
        "using ASCII-capable keyboard layout '%s' for IME fallback",
        inputSourceIdOnTISThread(keyboardLayout.get()).c_str()
    );
    return copyUnicodeKeyLayoutDataOnTISThread(keyboardLayout.get());
  });
}

AutoCFArray copyKeyboardInputSources()
{
  return runTISOnMainThread([] {
    CFStringRef keys[] = {kTISPropertyInputSourceCategory};
    CFStringRef values[] = {kTISCategoryKeyboardInputSource};
    AutoCFDictionary filter(
        CFDictionaryCreate(nullptr, (const void **)keys, (const void **)values, 1, nullptr, nullptr), CFRelease
    );
    return AutoCFArray(TISCreateInputSourceList(filter.get(), false), CFRelease);
  });
}

bool currentKeyboardInputSourceCanBeEnabled()
{
  return runTISOnMainThread([] {
    AutoTISInputSourceRef source(TISCopyCurrentKeyboardInputSource(), CFRelease);
    return source && TISGetInputSourceProperty(source.get(), kTISPropertyInputSourceIsEnableCapable) != nullptr;
  });
}

OSStatus selectInputSource(TISInputSourceRef source)
{
  return runTISOnMainThread([source] { return TISSelectInputSource(source); });
}

} // namespace OSXInputSources
