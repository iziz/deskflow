/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClientInfoUpdateState.h"

#include <QTest>

class ClientInfoUpdateStateTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void cursorUpdateDoesNotSuppressRemoteMouse();
  void cursorUpdatesAreSingleFlightAndKeepLatest();
  void cursorAckDoesNotReleasePendingShapeSuppression();
};

void ClientInfoUpdateStateTests::cursorUpdateDoesNotSuppressRemoteMouse()
{
  ClientInfoUpdateState state;

  QVERIFY(state.beginCursorUpdate());
  QVERIFY(!state.shouldIgnoreMouse());
  QCOMPARE(state.pendingCount(), size_t{1});

  const auto acknowledgment = state.acknowledge();
  QVERIFY(acknowledgment.matched);
  QVERIFY(!acknowledgment.sendLatestCursor);
  QVERIFY(!state.shouldIgnoreMouse());
}

void ClientInfoUpdateStateTests::cursorUpdatesAreSingleFlightAndKeepLatest()
{
  ClientInfoUpdateState state;

  QVERIFY(state.beginCursorUpdate());
  for (int i = 0; i < 100; ++i) {
    QVERIFY(!state.beginCursorUpdate());
  }
  QCOMPARE(state.pendingCount(), size_t{1});

  const auto firstAcknowledgment = state.acknowledge();
  QVERIFY(firstAcknowledgment.matched);
  QVERIFY(firstAcknowledgment.sendLatestCursor);
  QVERIFY(state.beginCursorUpdate());
  QCOMPARE(state.pendingCount(), size_t{1});

  const auto finalAcknowledgment = state.acknowledge();
  QVERIFY(finalAcknowledgment.matched);
  QVERIFY(!finalAcknowledgment.sendLatestCursor);
  QCOMPARE(state.pendingCount(), size_t{0});
}

void ClientInfoUpdateStateTests::cursorAckDoesNotReleasePendingShapeSuppression()
{
  ClientInfoUpdateState state;

  QVERIFY(state.beginCursorUpdate());
  state.beginUpdate(ClientInfoUpdateState::Kind::Shape);
  QVERIFY(state.shouldIgnoreMouse());

  const auto cursorAcknowledgment = state.acknowledge();
  QVERIFY(cursorAcknowledgment.matched);
  QVERIFY(state.shouldIgnoreMouse());

  const auto shapeAcknowledgment = state.acknowledge();
  QVERIFY(shapeAcknowledgment.matched);
  QVERIFY(!state.shouldIgnoreMouse());
}

QTEST_MAIN(ClientInfoUpdateStateTests)

#include "ClientInfoUpdateStateTests.moc"
