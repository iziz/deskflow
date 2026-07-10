/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXClipboardTests.h"

#include "platform/OSXClipboard.h"
#include "platform/OSXClipboardUTF8Converter.h"

#include <QByteArray>

#include <array>
#include <limits>

void OSXClipboardTests::open()
{
  OSXClipboard clipboard;
  QVERIFY(clipboard.open(0));
  QVERIFY(clipboard.empty());
  clipboard.close();
}

void OSXClipboardTests::singleFormat()
{
  using enum IClipboard::Format;

  OSXClipboard clipboard;
  QVERIFY(clipboard.empty());
  clipboard.add(Text, m_testString);
  QVERIFY(clipboard.has(Text));
  QCOMPARE(clipboard.get(Text), m_testString);
}

void OSXClipboardTests::formatConvert_UTF8()
{
  OSXClipboardUTF8Converter converter;
  QCOMPARE(IClipboard::Format::Text, converter.getFormat());
  QCOMPARE(converter.getOSXFormat(), CFSTR("public.utf8-plain-text"));
  QCOMPARE(converter.fromIClipboard("test data\n"), "test data\r");
  QCOMPARE(converter.toIClipboard("test data\r"), "test data\n");
}

void OSXClipboardTests::estimatedBitmapDataSize()
{
  QCOMPARE(OSXClipboard::estimatedBitmapDataSize(6016, 3384), size_t{81'432'832});
  QCOMPARE(
      OSXClipboard::estimatedBitmapDataSize(std::numeric_limits<size_t>::max(), 2), std::numeric_limits<size_t>::max()
  );
}

void OSXClipboardTests::bitmapPreflightUsesDeclaredImageDimensions()
{
  const auto png = QByteArray::fromBase64(
      "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mNk+A8AAQUBAScY42YAAAAASUVORK5CYII="
  );
  PasteboardRef pasteboard = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &pasteboard), noErr);
  QCOMPARE(PasteboardClear(pasteboard), noErr);

  const auto data = CFDataCreate(
      kCFAllocatorDefault, reinterpret_cast<const UInt8 *>(png.constData()), static_cast<CFIndex>(png.size())
  );
  QVERIFY(data != nullptr);
  const auto itemId = reinterpret_cast<PasteboardItemID>(pasteboard);
  QCOMPARE(PasteboardPutItemFlavor(pasteboard, itemId, CFSTR("public.png"), data, kPasteboardFlavorNoFlags), noErr);
  CFRelease(data);

  PasteboardItemID item = nullptr;
  QCOMPARE(PasteboardGetItemIdentifier(pasteboard, 1, &item), noErr);
  PasteboardFlavorFlags flags = 0;
  QCOMPARE(PasteboardGetItemFlavorFlags(pasteboard, item, CFSTR("com.microsoft.bmp"), &flags), noErr);

  OSXClipboard clipboard;
  clipboard.setMaximumDataSize(260);
  QVERIFY(!clipboard.has(IClipboard::Format::Bitmap));
  clipboard.setMaximumDataSize(261);
  QVERIFY(clipboard.has(IClipboard::Format::Bitmap));

  CFRelease(pasteboard);
}

void OSXClipboardTests::bitmapPreflightRejectsUnboundedNativeFormat()
{
  constexpr std::array<UInt8, 14> bmpHeader = {'B', 'M'};
  PasteboardRef pasteboard = nullptr;
  QCOMPARE(PasteboardCreate(kPasteboardClipboard, &pasteboard), noErr);
  QCOMPARE(PasteboardClear(pasteboard), noErr);

  const auto data = CFDataCreate(kCFAllocatorDefault, bmpHeader.data(), bmpHeader.size());
  QVERIFY(data != nullptr);
  const auto itemId = reinterpret_cast<PasteboardItemID>(pasteboard);
  QCOMPARE(
      PasteboardPutItemFlavor(pasteboard, itemId, CFSTR("com.microsoft.bmp"), data, kPasteboardFlavorNoFlags), noErr
  );
  CFRelease(data);

  OSXClipboard clipboard;
  clipboard.setMaximumDataSize(3 * 1024 * 1024);
  QVERIFY(!clipboard.has(IClipboard::Format::Bitmap));

  CFRelease(pasteboard);
}

QTEST_MAIN(OSXClipboardTests)
