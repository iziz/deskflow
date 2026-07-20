/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardWireCodec.h"

#include <QTest>

class ClipboardWireCodecTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void compressesAndRestoresLargeClipboard();
  void leavesSmallClipboardUncompressed();
  void rejectsDecodedClipboardBeyondLimit();
  void rejectsTruncatedCompressedClipboard();
  void keepsLegacyPayloadUnchanged();
};

void ClipboardWireCodecTests::compressesAndRestoresLargeClipboard()
{
  const std::string source(8 * 1024 * 1024, '\0');
  const auto encoded = deskflow::encodeClipboardWirePayload(source, true);

  QVERIFY(encoded.size() < source.size());
  const auto decoded = deskflow::decodeClipboardWirePayload(encoded, true, source.size());
  QVERIFY(decoded.valid);
  QVERIFY(decoded.compressed);
  QCOMPARE(decoded.data, source);
}

void ClipboardWireCodecTests::leavesSmallClipboardUncompressed()
{
  const std::string source = "small clipboard";
  const auto encoded = deskflow::encodeClipboardWirePayload(source, true);

  QCOMPARE(encoded, source);
  const auto decoded = deskflow::decodeClipboardWirePayload(encoded, true, source.size());
  QVERIFY(decoded.valid);
  QVERIFY(!decoded.compressed);
  QCOMPARE(decoded.data, source);
}

void ClipboardWireCodecTests::rejectsDecodedClipboardBeyondLimit()
{
  const std::string source(1024 * 1024, 'x');
  const auto encoded = deskflow::encodeClipboardWirePayload(source, true);

  const auto decoded = deskflow::decodeClipboardWirePayload(encoded, true, source.size() - 1);
  QVERIFY(!decoded.valid);
}

void ClipboardWireCodecTests::rejectsTruncatedCompressedClipboard()
{
  const std::string truncated = "DFZ1\0\0";
  const auto decoded = deskflow::decodeClipboardWirePayload(truncated, true, 1024);
  QVERIFY(!decoded.valid);
}

void ClipboardWireCodecTests::keepsLegacyPayloadUnchanged()
{
  const std::string source(1024, 'x');
  const auto encoded = deskflow::encodeClipboardWirePayload(source, false);
  QCOMPARE(encoded, source);

  const auto decoded = deskflow::decodeClipboardWirePayload(encoded, false, source.size());
  QVERIFY(decoded.valid);
  QVERIFY(!decoded.compressed);
  QCOMPARE(decoded.data, source);
}

QTEST_MAIN(ClipboardWireCodecTests)

#include "ClipboardWireCodecTests.moc"
