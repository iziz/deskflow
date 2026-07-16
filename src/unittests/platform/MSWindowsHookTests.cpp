/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsHookTests.h"

#include "platform/MSWindowsKeyEventPolicy.h"
#include "platform/MSWindowsMouseEventPolicy.h"

void MSWindowsHookTests::windowsHotKeyRegistration_data()
{
  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<quint64>("modifiers");
  QTest::addColumn<bool>("expected");

  QTest::newRow("unmodified Caps Lock") << quint64(VK_CAPITAL) << quint64(0) << false;
  QTest::newRow("unmodified Num Lock") << quint64(VK_NUMLOCK) << quint64(0) << false;
  QTest::newRow("unmodified Scroll Lock") << quint64(VK_SCROLL) << quint64(0) << false;
  QTest::newRow("modified Scroll Lock") << quint64(VK_SCROLL) << quint64(MOD_CONTROL) << true;
  QTest::newRow("regular key") << quint64('A') << quint64(0) << true;
}

void MSWindowsHookTests::windowsHotKeyRegistration()
{
  QFETCH(quint64, virtualKey);
  QFETCH(quint64, modifiers);
  QFETCH(bool, expected);

  QCOMPARE(
      deskflow::platform::shouldRegisterHotKeyWithWindows(
          static_cast<UINT>(virtualKey), static_cast<UINT>(modifiers)
      ),
      expected
  );
}

void MSWindowsHookTests::relaySuppression_data()
{
  QTest::addColumn<int>("mode");
  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<qint64>("keyInfo");
  QTest::addColumn<bool>("lowLevelHookActive");
  QTest::addColumn<bool>("expected");

  QTest::newRow("shift-space Hangul press")
      << int(kHOOK_RELAY_EVENTS) << quint64(VK_HANGUL) << qint64(0x00390001u) << true << true;
  QTest::newRow("shift-space Hangul release")
      << int(kHOOK_RELAY_EVENTS) << quint64(VK_HANGUL) << qint64(0x80390001u) << true << true;
  QTest::newRow("extended Space Hangul with low-level hook")
      << int(kHOOK_RELAY_EVENTS) << quint64(VK_HANGUL) << qint64(0x01390001u) << true << false;
  QTest::newRow("physical Hangul with low-level hook")
      << int(kHOOK_RELAY_EVENTS) << quint64(VK_HANGUL) << qint64(0x01720001u) << true << false;
  QTest::newRow("physical Hangul without low-level hook")
      << int(kHOOK_RELAY_EVENTS) << quint64(VK_HANGUL) << qint64(0x01720001u) << false << true;
  QTest::newRow("Caps Lock") << int(kHOOK_RELAY_EVENTS) << quint64(VK_CAPITAL) << qint64(0x003a0001u) << true << false;
  QTest::newRow("regular key") << int(kHOOK_RELAY_EVENTS) << quint64('A') << qint64(0x001e0001u) << true << true;
  QTest::newRow("local screen") << int(kHOOK_WATCH_JUMP_ZONE) << quint64(VK_HANGUL) << qint64(0x00390001u) << true
                                << false;
}

void MSWindowsHookTests::relaySuppression()
{
  QFETCH(int, mode);
  QFETCH(quint64, virtualKey);
  QFETCH(qint64, keyInfo);
  QFETCH(bool, lowLevelHookActive);
  QFETCH(bool, expected);

  QCOMPARE(
      deskflow::platform::shouldSuppressLocalKey(
          static_cast<EHookMode>(mode), static_cast<WPARAM>(virtualKey), static_cast<LPARAM>(keyInfo),
          lowLevelHookActive
      ),
      expected
  );
}

void MSWindowsHookTests::preRelayMouseMotion_data()
{
  QTest::addColumn<int>("mode");
  QTest::addColumn<quint64>("message");
  QTest::addColumn<quint64>("eventTime");
  QTest::addColumn<quint64>("relayCutoff");
  QTest::addColumn<bool>("hasRelayCutoff");
  QTest::addColumn<bool>("expected");

  QTest::newRow("older relay motion")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(990) << quint64(1000) << true << true;
  QTest::newRow("same-tick relay motion")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(1000) << quint64(1000) << true << true;
  QTest::newRow("new relay motion")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(1001) << quint64(1000) << true << false;
  QTest::newRow("local motion")
      << int(kHOOK_WATCH_JUMP_ZONE) << quint64(WM_MOUSEMOVE) << quint64(990) << quint64(1000) << true << false;
  QTest::newRow("relay button")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_LBUTTONDOWN) << quint64(990) << quint64(1000) << true << false;
  QTest::newRow("missing cutoff")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(990) << quint64(1000) << false << false;
  QTest::newRow("older motion across tick wrap")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(0xfffffff0u) << quint64(0x00000005u) << true
      << true;
  QTest::newRow("new motion across tick wrap")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(0x00000005u) << quint64(0xfffffff0u) << true
      << false;
}

void MSWindowsHookTests::preRelayMouseMotion()
{
  QFETCH(int, mode);
  QFETCH(quint64, message);
  QFETCH(quint64, eventTime);
  QFETCH(quint64, relayCutoff);
  QFETCH(bool, hasRelayCutoff);
  QFETCH(bool, expected);

  QCOMPARE(
      deskflow::platform::shouldDropPreRelayMouseMotion(
          static_cast<EHookMode>(mode), static_cast<WPARAM>(message), static_cast<DWORD>(eventTime),
          static_cast<DWORD>(relayCutoff), hasRelayCutoff
      ),
      expected
  );
}

QTEST_MAIN(MSWindowsHookTests)
