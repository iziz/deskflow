/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/PortalClipboard.h"

#include <QTest>

#include <array>
#include <unistd.h>

using deskflow::PortalClipboard;

class PortalClipboardTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void cleanEofCompletesRead();
  void exactLimitRequiresCleanEof();
  void openWriterTimesOutWithoutReturningPrefix();
  void oversizedReadIsRejected();
};

namespace {
std::array<int, 2> makePipe()
{
  std::array<int, 2> descriptors{-1, -1};
  if (::pipe(descriptors.data()) != 0) {
    return {-1, -1};
  }
  return descriptors;
}

void writeAll(int fd, const QByteArray &data)
{
  const auto written = ::write(fd, data.constData(), static_cast<size_t>(data.size()));
  QCOMPARE(written, static_cast<ssize_t>(data.size()));
}
} // namespace

void PortalClipboardTests::cleanEofCompletesRead()
{
  auto descriptors = makePipe();
  QVERIFY(descriptors[0] >= 0);
  writeAll(descriptors[1], QByteArrayLiteral("clipboard"));
  ::close(descriptors[1]);

  const auto result = PortalClipboard::readSelectionFd(descriptors[0], 64);

  QCOMPARE(result.status, PortalClipboard::SelectionReadStatus::Complete);
  QCOMPARE(result.data, QByteArrayLiteral("clipboard"));
}

void PortalClipboardTests::exactLimitRequiresCleanEof()
{
  auto descriptors = makePipe();
  QVERIFY(descriptors[0] >= 0);
  writeAll(descriptors[1], QByteArrayLiteral("four"));
  ::close(descriptors[1]);

  const auto result = PortalClipboard::readSelectionFd(descriptors[0], 4);

  QCOMPARE(result.status, PortalClipboard::SelectionReadStatus::Complete);
  QCOMPARE(result.data, QByteArrayLiteral("four"));
}

void PortalClipboardTests::openWriterTimesOutWithoutReturningPrefix()
{
  auto descriptors = makePipe();
  QVERIFY(descriptors[0] >= 0);
  writeAll(descriptors[1], QByteArrayLiteral("partial"));

  const auto result = PortalClipboard::readSelectionFd(descriptors[0], 64);
  ::close(descriptors[1]);

  QCOMPARE(result.status, PortalClipboard::SelectionReadStatus::Timeout);
  QVERIFY(result.data.isEmpty());
}

void PortalClipboardTests::oversizedReadIsRejected()
{
  auto descriptors = makePipe();
  QVERIFY(descriptors[0] >= 0);
  writeAll(descriptors[1], QByteArrayLiteral("large"));
  ::close(descriptors[1]);

  const auto result = PortalClipboard::readSelectionFd(descriptors[0], 4);

  QCOMPARE(result.status, PortalClipboard::SelectionReadStatus::TooLarge);
  QVERIFY(result.data.isEmpty());
}

QTEST_MAIN(PortalClipboardTests)

#include "PortalClipboardTests.moc"
