/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXClipboard.h"

#include "arch/ArchException.h"
#include "base/Log.h"
#include "platform/OSXClipboardBMPConverter.h"
#include "platform/OSXClipboardHTMLConverter.h"
#include "platform/OSXClipboardTextConverter.h"
#include "platform/OSXClipboardUTF16Converter.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <ImageIO/ImageIO.h>

#include <array>
#include <optional>
#include <utility>

namespace {
CFStringRef deskflowOwnershipFlavor()
{
  return CFSTR("org.deskflow.clipboard-owner");
}

std::optional<std::pair<size_t, size_t>> imageDimensions(PasteboardRef pasteboard, PasteboardItemID item)
{
  const std::array<CFStringRef, 3> sourceTypes = {CFSTR("public.png"), CFSTR("public.jpeg"), CFSTR("public.tiff")};

  CFArrayRef declaredTypes = nullptr;
  if (PasteboardCopyItemFlavors(pasteboard, item, &declaredTypes) != noErr || declaredTypes == nullptr) {
    return std::nullopt;
  }

  std::optional<std::pair<size_t, size_t>> result;
  for (const auto type : sourceTypes) {
    if (!CFArrayContainsValue(declaredTypes, CFRangeMake(0, CFArrayGetCount(declaredTypes)), type)) {
      continue;
    }

    CFDataRef data = nullptr;
    if (PasteboardCopyItemFlavorData(pasteboard, item, type, &data) != noErr || data == nullptr) {
      continue;
    }

    CGImageSourceRef source = CGImageSourceCreateWithData(data, nullptr);
    CFRelease(data);
    if (source == nullptr) {
      continue;
    }

    CFDictionaryRef properties = CGImageSourceCopyPropertiesAtIndex(source, 0, nullptr);
    CFRelease(source);
    if (properties == nullptr) {
      continue;
    }

    int64_t width = 0;
    int64_t height = 0;
    const auto widthValue = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelWidth));
    const auto heightValue = static_cast<CFNumberRef>(CFDictionaryGetValue(properties, kCGImagePropertyPixelHeight));
    const bool valid = widthValue != nullptr && heightValue != nullptr &&
                       CFNumberGetValue(widthValue, kCFNumberSInt64Type, &width) &&
                       CFNumberGetValue(heightValue, kCFNumberSInt64Type, &height) && width > 0 && height > 0;
    CFRelease(properties);

    if (valid) {
      result = std::pair<size_t, size_t>{static_cast<size_t>(width), static_cast<size_t>(height)};
      break;
    }
  }

  CFRelease(declaredTypes);
  return result;
}
} // namespace

//
// OSXClipboard
//

OSXClipboard::OSXClipboard() : m_time(0), m_pboard(nullptr)
{
  m_converters.push_back(new OSXClipboardHTMLConverter);
  m_converters.push_back(new OSXClipboardBMPConverter);
  m_converters.push_back(new OSXClipboardUTF8Converter);
  m_converters.push_back(new OSXClipboardUTF16Converter);
  m_converters.push_back(new OSXClipboardTextConverter);

  OSStatus createErr = PasteboardCreate(kPasteboardClipboard, &m_pboard);
  if (createErr != noErr) {
    LOG_WARN("failed to create clipboard reference: error %i", createErr);
    LOG_ERR("unable to connect to pasteboard, clipboard sharing disabled", createErr);
    m_pboard = nullptr;
    return;
  }

  OSStatus syncErr = PasteboardSynchronize(m_pboard);
  if (syncErr != noErr) {
    LOG_WARN("failed to syncronize clipboard: error %i", syncErr);
  }
}

OSXClipboard::~OSXClipboard()
{
  clearConverters();
  if (m_pboard != nullptr) {
    CFRelease(m_pboard);
    m_pboard = nullptr;
  }
}

bool OSXClipboard::empty()
{
  LOG_DEBUG("emptying clipboard");
  if (m_pboard == nullptr)
    return false;

  OSStatus err = PasteboardClear(m_pboard);
  if (err != noErr) {
    LOG_WARN("failed to clear clipboard: error %i", err);
    return false;
  }

  markOwnedByDeskflow();
  return true;
}

bool OSXClipboard::isOwnedByDeskflow()
{
  PasteboardRef pboard = nullptr;
  if (PasteboardCreate(kPasteboardClipboard, &pboard) != noErr) {
    return false;
  }

  PasteboardSynchronize(pboard);

  PasteboardItemID item = nullptr;
  const bool hasItem = PasteboardGetItemIdentifier(pboard, 1, &item) == noErr;
  PasteboardFlavorFlags flags = 0;
  const bool isOwned =
      hasItem && PasteboardGetItemFlavorFlags(pboard, item, deskflowOwnershipFlavor(), &flags) == noErr;

  CFRelease(pboard);
  return isOwned;
}

void OSXClipboard::markOwnedByDeskflow() const
{
  const uint8_t marker = 1;
  CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, &marker, sizeof(marker));
  if (dataRef == nullptr) {
    LOG_WARN("failed to create Deskflow clipboard ownership marker");
    return;
  }

  PasteboardItemID itemID = 0;
  OSStatus err =
      PasteboardPutItemFlavor(m_pboard, itemID, deskflowOwnershipFlavor(), dataRef, kPasteboardFlavorNoFlags);
  CFRelease(dataRef);

  if (err != noErr) {
    LOG_WARN("failed to mark clipboard as Deskflow-owned: error %i", err);
  }
}

bool OSXClipboard::synchronize()
{
  if (m_pboard == nullptr)
    return false;

  PasteboardSyncFlags flags = PasteboardSynchronize(m_pboard);
  LOG_VERBOSE("flags: %x", flags);

  if (flags & kPasteboardModified) {
    return true;
  }
  return false;
}

void OSXClipboard::add(Format format, const std::string &data)
{
  if (m_pboard == nullptr)
    return;

  LOG_DEBUG("add %d bytes to clipboard format: %d", data.size(), format);
  if (format == IClipboard::Format::Text) {
    LOG_DEBUG("format of data to be added to clipboard was kText");
  } else if (format == IClipboard::Format::Bitmap) {
    LOG_DEBUG("format of data to be added to clipboard was kBitmap");
  } else if (format == IClipboard::Format::HTML) {
    LOG_DEBUG("format of data to be added to clipboard was kHTML");
  }

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {

    IOSXClipboardConverter *converter = *index;

    // skip converters for other formats
    if (converter->getFormat() == format) {
      std::string osXData = converter->fromIClipboard(data);
      CFStringRef flavorType = converter->getOSXFormat();
      CFDataRef dataRef = CFDataCreate(kCFAllocatorDefault, (uint8_t *)osXData.data(), osXData.size());
      PasteboardItemID itemID = 0;

      if (dataRef) {
        PasteboardPutItemFlavor(m_pboard, itemID, flavorType, dataRef, kPasteboardFlavorNoFlags);

        CFRelease(dataRef);
        LOG_DEBUG("added %d bytes to clipboard format: %d", data.size(), format);
      }
    }
  }
}

bool OSXClipboard::open(Time time) const
{
  if (m_pboard == nullptr)
    return false;

  LOG_DEBUG("opening clipboard");
  m_time = time;
  return true;
}

void OSXClipboard::close() const
{
  LOG_DEBUG("closing clipboard");
  /* not needed */
}

IClipboard::Time OSXClipboard::getTime() const
{
  return m_time;
}

bool OSXClipboard::has(Format format) const
{
  if (m_pboard == nullptr)
    return false;

  PasteboardItemID item;
  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    IOSXClipboardConverter *converter = *index;
    if (converter->getFormat() == format) {
      PasteboardFlavorFlags flags;
      CFStringRef type = converter->getOSXFormat();

      OSStatus res;

      if ((res = PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags)) == noErr) {
        if (format == Format::Bitmap && m_maximumDataSize != std::numeric_limits<size_t>::max()) {
          const auto dimensions = imageDimensions(m_pboard, item);
          if (!dimensions.has_value()) {
            LOG_WARN("skipping macOS clipboard image because its BMP size cannot be bounded safely");
            return false;
          }

          const auto estimatedSize = estimatedBitmapDataSize(dimensions->first, dimensions->second);
          if (estimatedSize >= m_maximumDataSize) {
            LOG_WARN(
                "skipping macOS clipboard image before BMP materialization: dimensions=%zux%zu estimated=%zu "
                "limit=%zu",
                dimensions->first, dimensions->second, estimatedSize, m_maximumDataSize
            );
            return false;
          }
        }
        return true;
      }
    }
  }

  return false;
}

std::string OSXClipboard::get(Format format) const
{
  CFStringRef type;
  PasteboardItemID item;
  std::string result;

  if (m_pboard == nullptr)
    return result;

  PasteboardGetItemIdentifier(m_pboard, (CFIndex)1, &item);

  // find the converter for the first clipboard format we can handle
  IOSXClipboardConverter *converter = nullptr;
  for (ConverterList::const_iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    converter = *index;

    PasteboardFlavorFlags flags;
    type = converter->getOSXFormat();

    if (converter->getFormat() == format && PasteboardGetItemFlavorFlags(m_pboard, item, type, &flags) == noErr) {
      break;
    }
    converter = nullptr;
  }

  // if no converter then we don't recognize any formats
  if (converter == nullptr) {
    LOG_DEBUG("unable to find converter for data");
    return result;
  }

  // get the clipboard data.
  CFDataRef buffer = nullptr;
  try {
    OSStatus err = PasteboardCopyItemFlavorData(m_pboard, item, type, &buffer);

    if (err != noErr) {
      throw err;
    }

    result = std::string((char *)CFDataGetBytePtr(buffer), CFDataGetLength(buffer));
  } catch (OSStatus err) {
    LOG_DEBUG("exception thrown in OSXClipboard::get MacError (%d)", err);
  } catch (...) {
    LOG_DEBUG("unknown exception in OSXClipboard::get");
    RETHROW_THREADEXCEPTION
  }

  if (buffer != nullptr)
    CFRelease(buffer);

  return converter->toIClipboard(result);
}

void OSXClipboard::clearConverters()
{
  for (ConverterList::iterator index = m_converters.begin(); index != m_converters.end(); ++index) {
    delete *index;
  }
  m_converters.clear();
}

void OSXClipboard::setMaximumDataSize(size_t maximumDataSize)
{
  m_maximumDataSize = maximumDataSize;
}

size_t OSXClipboard::estimatedBitmapDataSize(size_t width, size_t height)
{
  constexpr size_t headerAndProtocolOverhead = 256;
  constexpr size_t bytesPerPixel = 4;
  if (height != 0 &&
      width > (std::numeric_limits<size_t>::max() - headerAndProtocolOverhead) / bytesPerPixel / height) {
    return std::numeric_limits<size_t>::max();
  }
  return width * height * bytesPerPixel + headerAndProtocolOverhead;
}
