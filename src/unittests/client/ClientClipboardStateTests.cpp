/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClientClipboardState.h"
#include "base/Log.h"

#include <QTest>

class ClientClipboardStateTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void remoteRevisionRejectsOlderSequence();
  void localSendTracksPendingAndAcknowledgedStates();
  void failedTransferForgetsSentCache();
  void remoteApplySuppressesMatchingGrab();

private:
  Log m_log;
};

void ClientClipboardStateTests::remoteRevisionRejectsOlderSequence()
{
  ClientClipboardState state;

  state.markRemoteClipboardApplied(kClipboardClipboard, 12, "remote");

  QVERIFY(state.isRemoteRevisionStale(kClipboardClipboard, 11));
  QVERIFY(state.isRemoteRevisionStale(kClipboardClipboard, 0xffffffffu));
  QVERIFY(!state.isRemoteRevisionStale(kClipboardClipboard, 13));
  QVERIFY(!state.isRemoteRevisionStale(kClipboardClipboard, 0));
}

void ClientClipboardStateTests::localSendTracksPendingAndAcknowledgedStates()
{
  ClientClipboardState state;

  const auto firstSend = state.markLocalClipboardReadForSend(kClipboardClipboard, 10, "payload");
  QVERIFY(firstSend.shouldSend);
  QVERIFY(firstSend.force);
  QCOMPARE(state.cacheState(kClipboardClipboard), ClientClipboardState::CacheState::AwaitingServerAck);

  const auto duplicateSend = state.markLocalClipboardReadForSend(kClipboardClipboard, 11, "payload");
  QVERIFY(!duplicateSend.shouldSend);
  QVERIFY(!duplicateSend.force);
  QCOMPARE(state.cacheState(kClipboardClipboard), ClientClipboardState::CacheState::AwaitingServerAck);

  state.markLocalClipboardAcknowledged(kClipboardClipboard);
  QCOMPARE(state.cacheState(kClipboardClipboard), ClientClipboardState::CacheState::ServerAcknowledged);
}

void ClientClipboardStateTests::failedTransferForgetsSentCache()
{
  ClientClipboardState state;

  state.markLocalClipboardReadForSend(kClipboardClipboard, 10, "payload");
  state.forgetSentClipboard(kClipboardClipboard);

  QCOMPARE(state.cacheState(kClipboardClipboard), ClientClipboardState::CacheState::Empty);
  QCOMPARE(state.clipboardTime(kClipboardClipboard), 0);

  const auto resend = state.markLocalClipboardReadForSend(kClipboardClipboard, 12, "payload");
  QVERIFY(resend.shouldSend);
  QVERIFY(resend.force);
}

void ClientClipboardStateTests::remoteApplySuppressesMatchingGrab()
{
  ClientClipboardState state;

  state.markRemoteClipboardApplied(kClipboardClipboard, 5, "remote");
  state.updateCachedClipboard(kClipboardClipboard, 42, "remote");

  QVERIFY(state.shouldIgnoreGrabbedClipboard(kClipboardClipboard, "remote"));
  QVERIFY(!state.shouldIgnoreGrabbedClipboard(kClipboardClipboard, "local"));

  state.markGrabbedClipboardIgnored(kClipboardClipboard, 43);
  QCOMPARE(state.clipboardTime(kClipboardClipboard), 43);
}

QTEST_MAIN(ClientClipboardStateTests)

#include "ClientClipboardStateTests.moc"
