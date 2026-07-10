/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardChannelProtocol.h"

#include <QTest>

using deskflow::classifyClipboardChannelHello;
using deskflow::ClipboardChannelHelloKind;
using deskflow::ClipboardChannelToken;

class ClipboardChannelProtocolTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void acceptsStandardControlHello();
  void acceptsSamePacketClipboardExtension();
  void doesNotTreatFollowingPacketAsExtension();
  void rejectsPartialOrTrailingExtension();
  void acceptsTokenOnlyOnce();
  void rejectsWrongAndExpiredTokens();
};

void ClipboardChannelProtocolTests::acceptsStandardControlHello()
{
  constexpr uint32_t kClientNameSize = 6;
  constexpr uint32_t kControlPacketSize = 7 + 2 + 2 + 4 + kClientNameSize;
  QCOMPARE(classifyClipboardChannelHello(kControlPacketSize, kClientNameSize), ClipboardChannelHelloKind::Control);
}

void ClipboardChannelProtocolTests::acceptsSamePacketClipboardExtension()
{
  constexpr uint32_t kClientNameSize = 6;
  constexpr uint32_t kControlPacketSize = 7 + 2 + 2 + 4 + kClientNameSize;
  constexpr uint32_t kExtensionSize = 4 + 4 + kClipboardChannelTokenSize;
  QCOMPARE(
      classifyClipboardChannelHello(kControlPacketSize + kExtensionSize, kClientNameSize),
      ClipboardChannelHelloKind::Clipboard
  );
}

void ClipboardChannelProtocolTests::doesNotTreatFollowingPacketAsExtension()
{
  constexpr uint32_t kClientNameSize = 6;
  constexpr uint32_t kFirstPacketSize = 7 + 2 + 2 + 4 + kClientNameSize;

  // The size of a pipelined or split second packet is deliberately absent:
  // classification is scoped to the first PacketStreamFilter frame.
  QCOMPARE(classifyClipboardChannelHello(kFirstPacketSize, kClientNameSize), ClipboardChannelHelloKind::Control);
}

void ClipboardChannelProtocolTests::rejectsPartialOrTrailingExtension()
{
  constexpr uint32_t kClientNameSize = 6;
  constexpr uint32_t kControlPacketSize = 7 + 2 + 2 + 4 + kClientNameSize;
  constexpr uint32_t kExtensionSize = 4 + 4 + kClipboardChannelTokenSize;
  QCOMPARE(
      classifyClipboardChannelHello(kControlPacketSize + kExtensionSize - 1, kClientNameSize),
      ClipboardChannelHelloKind::Invalid
  );
  QCOMPARE(
      classifyClipboardChannelHello(kControlPacketSize + kExtensionSize + 1, kClientNameSize),
      ClipboardChannelHelloKind::Invalid
  );
}

void ClipboardChannelProtocolTests::acceptsTokenOnlyOnce()
{
  ClipboardChannelToken token;
  const auto now = ClipboardChannelToken::TimePoint{};
  const std::string value(kClipboardChannelTokenSize, 'x');
  token.issue(value, now + std::chrono::seconds(30));

  QVERIFY(token.consume(value, now));
  QVERIFY(!token.consume(value, now));
}

void ClipboardChannelProtocolTests::rejectsWrongAndExpiredTokens()
{
  ClipboardChannelToken token;
  const auto now = ClipboardChannelToken::TimePoint{};
  const std::string value(kClipboardChannelTokenSize, 'x');
  const std::string wrong(kClipboardChannelTokenSize, 'y');
  token.issue(value, now + std::chrono::seconds(30));

  QVERIFY(!token.consume(wrong, now));
  QVERIFY(token.consume(value, now));

  token.issue(value, now + std::chrono::seconds(30));
  QVERIFY(!token.consume(value, now + std::chrono::seconds(31)));
}

QTEST_MAIN(ClipboardChannelProtocolTests)

#include "ClipboardChannelProtocolTests.moc"
