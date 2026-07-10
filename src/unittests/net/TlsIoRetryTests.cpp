/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "net/TlsIoRetry.h"

#include <QTest>

class TlsIoRetryTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void writeWantReadRetriesWriteOnReadable();
  void readWantWriteRetriesReadOnWritable();
  void sameOperationCanChangePollDirection();
  void completionRestoresIdleState();
  void completedReadWithoutPendingPlaintextDoesNotSpeculativelyRead();
  void bufferedPlaintextCanBeDrainedWithinInputLimit();
  void inputLimitStopsBufferedPlaintextDrain();
};

void TlsIoRetryTests::writeWantReadRetriesWriteOnReadable()
{
  using Operation = deskflow::TlsIoRetry::Operation;
  using WaitFor = deskflow::TlsIoRetry::WaitFor;

  deskflow::TlsIoRetry retry;
  retry.set(Operation::Write, WaitFor::Readable);

  QVERIFY(retry.pending());
  QVERIFY(retry.wantsRead());
  QVERIFY(!retry.wantsWrite());
  QVERIFY(retry.canRetry(Operation::Write, true, false));
  QVERIFY(!retry.canRetry(Operation::Write, false, true));
  QVERIFY(!retry.canRetry(Operation::Read, true, true));
}

void TlsIoRetryTests::readWantWriteRetriesReadOnWritable()
{
  using Operation = deskflow::TlsIoRetry::Operation;
  using WaitFor = deskflow::TlsIoRetry::WaitFor;

  deskflow::TlsIoRetry retry;
  retry.set(Operation::Read, WaitFor::Writable);

  QVERIFY(retry.pending());
  QVERIFY(!retry.wantsRead());
  QVERIFY(retry.wantsWrite());
  QVERIFY(retry.canRetry(Operation::Read, false, true));
  QVERIFY(!retry.canRetry(Operation::Read, true, false));
  QVERIFY(!retry.canRetry(Operation::Write, true, true));
}

void TlsIoRetryTests::sameOperationCanChangePollDirection()
{
  using Operation = deskflow::TlsIoRetry::Operation;
  using WaitFor = deskflow::TlsIoRetry::WaitFor;

  deskflow::TlsIoRetry retry;
  retry.set(Operation::Write, WaitFor::Readable);
  retry.set(Operation::Write, WaitFor::Writable);

  QCOMPARE(retry.operation(), Operation::Write);
  QCOMPARE(retry.waitFor(), WaitFor::Writable);
  QVERIFY(!retry.canRetry(Operation::Write, true, false));
  QVERIFY(retry.canRetry(Operation::Write, false, true));
}

void TlsIoRetryTests::completionRestoresIdleState()
{
  using Operation = deskflow::TlsIoRetry::Operation;
  using WaitFor = deskflow::TlsIoRetry::WaitFor;

  deskflow::TlsIoRetry retry;
  retry.set(Operation::Read, WaitFor::Readable);
  retry.complete(Operation::Read);

  QVERIFY(!retry.pending());
  QCOMPARE(retry.operation(), Operation::None);
  QCOMPARE(retry.waitFor(), WaitFor::None);
  QVERIFY(!retry.wantsRead());
  QVERIFY(!retry.wantsWrite());
}

void TlsIoRetryTests::completedReadWithoutPendingPlaintextDoesNotSpeculativelyRead()
{
  QVERIFY(!deskflow::shouldDrainTlsPlaintext(128, 1024, 0));
}

void TlsIoRetryTests::bufferedPlaintextCanBeDrainedWithinInputLimit()
{
  QVERIFY(deskflow::shouldDrainTlsPlaintext(128, 1024, 64));
  QVERIFY(deskflow::shouldDrainTlsPlaintext(1024, 1024, 1));
}

void TlsIoRetryTests::inputLimitStopsBufferedPlaintextDrain()
{
  QVERIFY(!deskflow::shouldDrainTlsPlaintext(1025, 1024, 64));
}

QTEST_MAIN(TlsIoRetryTests)

#include "TlsIoRetryTests.moc"
