/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsHookTests.h"

#include "platform/MSWindowsKeyEventPolicy.h"
#include "platform/MSWindowsMouseEventPolicy.h"

void MSWindowsHookTests::advanceToggleKeyState_data()
{
  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<bool>("keyDown");
  QTest::addColumn<bool>("wasDown");
  QTest::addColumn<bool>("expected");

  QTest::newRow("Caps Lock press") << quint64(VK_CAPITAL) << true << false << true;
  QTest::newRow("Num Lock press") << quint64(VK_NUMLOCK) << true << false << true;
  QTest::newRow("Scroll Lock press") << quint64(VK_SCROLL) << true << false << true;
  QTest::newRow("Scroll Lock repeat") << quint64(VK_SCROLL) << true << true << false;
  QTest::newRow("Scroll Lock release") << quint64(VK_SCROLL) << false << true << false;
  QTest::newRow("regular key press") << quint64('A') << true << false << false;
}

void MSWindowsHookTests::advanceToggleKeyState()
{
  QFETCH(quint64, virtualKey);
  QFETCH(bool, keyDown);
  QFETCH(bool, wasDown);
  QFETCH(bool, expected);

  QCOMPARE(
      deskflow::platform::shouldAdvanceWindowsToggleKeyState(static_cast<UINT>(virtualKey), keyDown, wasDown), expected
  );
}

void MSWindowsHookTests::virtualKeyHotKeyRouting_data()
{
  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<int>("expected");

  const auto modifierOnly = static_cast<int>(deskflow::platform::WindowsHotKeyRoute::ModifierOnly);
  const auto virtualKeyRoute = static_cast<int>(deskflow::platform::WindowsHotKeyRoute::VirtualKey);

  QTest::newRow("Shift") << quint64(VK_SHIFT) << modifierOnly;
  QTest::newRow("left Control") << quint64(VK_LCONTROL) << modifierOnly;
  QTest::newRow("right Alt") << quint64(VK_RMENU) << modifierOnly;
  QTest::newRow("left Windows") << quint64(VK_LWIN) << modifierOnly;
  QTest::newRow("regular key") << quint64('A') << virtualKeyRoute;
  QTest::newRow("Caps Lock") << quint64(VK_CAPITAL) << virtualKeyRoute;
  QTest::newRow("Num Lock") << quint64(VK_NUMLOCK) << virtualKeyRoute;
  QTest::newRow("Scroll Lock") << quint64(VK_SCROLL) << virtualKeyRoute;
}

void MSWindowsHookTests::virtualKeyHotKeyRouting()
{
  QFETCH(quint64, virtualKey);
  QFETCH(int, expected);

  QCOMPARE(
      static_cast<int>(deskflow::platform::windowsHotKeyRoute(static_cast<UINT>(virtualKey))), expected
  );
}

void MSWindowsHookTests::toggleKeyTransitionSequence()
{
  KeyModifierMask modifiers = KeyModifierNumLock;

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, true, false);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock | KeyModifierScrollLock));

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, true, true);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock | KeyModifierScrollLock));

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, false, true);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock | KeyModifierScrollLock));

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, true, false);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock));
}

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
      deskflow::platform::shouldRegisterHotKeyWithWindows(static_cast<UINT>(virtualKey), static_cast<UINT>(modifiers)),
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

void MSWindowsHookTests::preModeMouseEvent_data()
{
  QTest::addColumn<int>("mode");
  QTest::addColumn<quint64>("message");
  QTest::addColumn<quint64>("eventTime");
  QTest::addColumn<quint64>("modeCutoff");
  QTest::addColumn<bool>("hasModeCutoff");
  QTest::addColumn<int>("staleButtonAction");
  QTest::addColumn<int>("expected");

  QTest::newRow("older relay motion") << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(990)
                                      << quint64(1000) << true
                                      << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
                                      << int(deskflow::platform::PreModeMouseEventAction::Suppress);
  QTest::newRow("same-tick relay motion")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(1000) << quint64(1000) << true
      << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
      << int(deskflow::platform::PreModeMouseEventAction::Suppress);
  QTest::newRow("new relay motion") << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(1001)
                                    << quint64(1000) << true
                                    << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
                                    << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("older local motion") << int(kHOOK_WATCH_JUMP_ZONE) << quint64(WM_MOUSEMOVE) << quint64(990)
                                      << quint64(1000) << true
                                      << int(deskflow::platform::PreModeMouseEventAction::Suppress)
                                      << int(deskflow::platform::PreModeMouseEventAction::Suppress);
  QTest::newRow("new local motion") << int(kHOOK_WATCH_JUMP_ZONE) << quint64(WM_MOUSEMOVE) << quint64(1001)
                                    << quint64(1000) << true
                                    << int(deskflow::platform::PreModeMouseEventAction::Suppress)
                                    << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("disabled hook") << int(kHOOK_DISABLE) << quint64(WM_MOUSEMOVE) << quint64(990) << quint64(1000) << true
                                 << int(deskflow::platform::PreModeMouseEventAction::Suppress)
                                 << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("older local button before relay")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_LBUTTONDOWN) << quint64(990) << quint64(1000) << true
      << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
      << int(deskflow::platform::PreModeMouseEventAction::PassThrough);
  QTest::newRow("same-tick relay button before local")
      << int(kHOOK_WATCH_JUMP_ZONE) << quint64(WM_RBUTTONUP) << quint64(1000) << quint64(1000) << true
      << int(deskflow::platform::PreModeMouseEventAction::Suppress)
      << int(deskflow::platform::PreModeMouseEventAction::Suppress);
  QTest::newRow("new relay button") << int(kHOOK_RELAY_EVENTS) << quint64(WM_XBUTTONDOWN) << quint64(1001)
                                    << quint64(1000) << true
                                    << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
                                    << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("older wheel") << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEWHEEL) << quint64(990) << quint64(1000)
                               << true << int(deskflow::platform::PreModeMouseEventAction::Suppress)
                               << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("missing cutoff") << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(990) << quint64(1000)
                                  << false << int(deskflow::platform::PreModeMouseEventAction::Suppress)
                                  << int(deskflow::platform::PreModeMouseEventAction::Process);
  QTest::newRow("older motion across tick wrap")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(0xfffffff0u) << quint64(0x00000005u) << true
      << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
      << int(deskflow::platform::PreModeMouseEventAction::Suppress);
  QTest::newRow("new motion across tick wrap")
      << int(kHOOK_RELAY_EVENTS) << quint64(WM_MOUSEMOVE) << quint64(0x00000005u) << quint64(0xfffffff0u) << true
      << int(deskflow::platform::PreModeMouseEventAction::PassThrough)
      << int(deskflow::platform::PreModeMouseEventAction::Process);
}

void MSWindowsHookTests::preModeMouseEvent()
{
  QFETCH(int, mode);
  QFETCH(quint64, message);
  QFETCH(quint64, eventTime);
  QFETCH(quint64, modeCutoff);
  QFETCH(bool, hasModeCutoff);
  QFETCH(int, staleButtonAction);
  QFETCH(int, expected);

  QCOMPARE(
      int(deskflow::platform::classifyPreModeMouseEvent(
          static_cast<EHookMode>(mode), static_cast<WPARAM>(message), static_cast<DWORD>(eventTime),
          static_cast<DWORD>(modeCutoff), hasModeCutoff,
          static_cast<deskflow::platform::PreModeMouseEventAction>(staleButtonAction)
      )),
      expected
  );
}

QTEST_MAIN(MSWindowsHookTests)
