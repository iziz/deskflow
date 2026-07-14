/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsHookTests.h"

#include "platform/MSWindowsKeyEventPolicy.h"

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

QTEST_MAIN(MSWindowsHookTests)
