/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "MSWindowsHookTests.h"

#include "platform/MSWindowsKeyEventPolicy.h"
#include "platform/MSWindowsMouseEventPolicy.h"

#include <iterator>
#include <string>

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

  QCOMPARE(static_cast<int>(deskflow::platform::windowsHotKeyRoute(static_cast<UINT>(virtualKey))), expected);
}

void MSWindowsHookTests::primaryKeyRestoreRouting_data()
{
  using enum deskflow::platform::WindowsPrimaryKeyRestoreRoute;

  QTest::addColumn<bool>("isOnScreen");
  QTest::addColumn<bool>("keyDown");
  QTest::addColumn<bool>("wasDown");
  QTest::addColumn<bool>("trackedForLocalRestore");
  QTest::addColumn<int>("expected");

  QTest::newRow("fresh remote press replaces stale restore")
      << false << true << false << true << static_cast<int>(ForgetStaleRestore);
  QTest::newRow("remote repeat keeps pre-switch restore") << false << true << true << true << static_cast<int>(Relay);
  QTest::newRow("pre-switch release restores locally")
      << false << false << true << true << static_cast<int>(RestoreLocally);
  QTest::newRow("reconciled pre-switch release restores locally")
      << false << false << false << true << static_cast<int>(RestoreLocally);
  QTest::newRow("untracked remote press relays") << false << true << false << false << static_cast<int>(Relay);
  QTest::newRow("on-screen release relays") << true << false << true << true << static_cast<int>(Relay);
}

void MSWindowsHookTests::primaryKeyRestoreRouting()
{
  QFETCH(bool, isOnScreen);
  QFETCH(bool, keyDown);
  QFETCH(bool, wasDown);
  QFETCH(bool, trackedForLocalRestore);
  QFETCH(int, expected);

  QCOMPARE(
      static_cast<int>(
          deskflow::platform::windowsPrimaryKeyRestoreRoute(isOnScreen, keyDown, wasDown, trackedForLocalRestore)
      ),
      expected
  );
}

void MSWindowsHookTests::primaryKeyRestoreFreshPressSequence()
{
  using enum deskflow::platform::WindowsPrimaryKeyRestoreRoute;

  bool trackedForLocalRestore = true;
  const auto freshControlDown =
      deskflow::platform::windowsPrimaryKeyRestoreRoute(false, true, false, trackedForLocalRestore);
  QCOMPARE(freshControlDown, ForgetStaleRestore);

  trackedForLocalRestore = false;
  const auto matchingControlUp =
      deskflow::platform::windowsPrimaryKeyRestoreRoute(false, false, true, trackedForLocalRestore);
  QCOMPARE(matchingControlUp, Relay);
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

void MSWindowsHookTests::nativeToggleStateReconciliation()
{
  KeyModifierMask modifiers = KeyModifierNumLock | KeyModifierScrollLock;

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, true, false);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock));

  modifiers = deskflow::platform::reconcileWindowsToggleKeyState(modifiers, VK_SCROLL, true);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock | KeyModifierScrollLock));

  modifiers = deskflow::platform::reconcileWindowsToggleKeyState(modifiers, VK_SCROLL, false);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock));
}

void MSWindowsHookTests::inputDesktopToggleSynchronization()
{
  const KeyModifierMask staleCoreThreadState = KeyModifierNumLock | KeyModifierScrollLock;
  const KeyModifierMask inputDesktopState = KeyModifierNumLock;

  KeyModifierMask modifiers = staleCoreThreadState;
  modifiers = deskflow::platform::synchronizeWindowsToggleKeyState(inputDesktopState | KeyModifierShift);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock));

  modifiers = deskflow::platform::advanceWindowsToggleKeyState(modifiers, VK_SCROLL, true, false);
  QCOMPARE(modifiers, KeyModifierMask(KeyModifierNumLock | KeyModifierScrollLock));
}

void MSWindowsHookTests::nativeKeyDownFlag_data()
{
  using namespace deskflow::platform;

  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<quint64>("expected");

  QTest::newRow("left Shift") << quint64(VK_LSHIFT) << quint64(WindowsNativeKeyStateLeftShift);
  QTest::newRow("right Shift") << quint64(VK_RSHIFT) << quint64(WindowsNativeKeyStateRightShift);
  QTest::newRow("left Control") << quint64(VK_LCONTROL) << quint64(WindowsNativeKeyStateLeftControl);
  QTest::newRow("right Control") << quint64(VK_RCONTROL) << quint64(WindowsNativeKeyStateRightControl);
  QTest::newRow("left Alt") << quint64(VK_LMENU) << quint64(WindowsNativeKeyStateLeftAlt);
  QTest::newRow("right Alt") << quint64(VK_RMENU) << quint64(WindowsNativeKeyStateRightAlt);
  QTest::newRow("left Windows") << quint64(VK_LWIN) << quint64(WindowsNativeKeyStateLeftSuper);
  QTest::newRow("right Windows") << quint64(VK_RWIN) << quint64(WindowsNativeKeyStateRightSuper);
  QTest::newRow("regular key") << quint64('A') << quint64(0);
}

void MSWindowsHookTests::nativeKeyDownFlag()
{
  QFETCH(quint64, virtualKey);
  QFETCH(quint64, expected);

  QCOMPARE(quint64(deskflow::platform::windowsNativeKeyDownFlag(static_cast<UINT>(virtualKey))), expected);
}

void MSWindowsHookTests::hookHeldKeyTransition_data()
{
  using namespace deskflow::platform;

  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<quint64>("expectedDown");
  QTest::addColumn<quint64>("expectedUp");

  constexpr uint32_t allHeld = WindowsNativeKeyStateLeftShift | WindowsNativeKeyStateRightShift |
                               WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl |
                               WindowsNativeKeyStateLeftAlt | WindowsNativeKeyStateRightAlt |
                               WindowsNativeKeyStateLeftSuper | WindowsNativeKeyStateRightSuper;

  QTest::newRow("left Shift") << quint64(VK_LSHIFT) << quint64(WindowsNativeKeyStateLeftShift)
                              << quint64(allHeld & ~WindowsNativeKeyStateLeftShift);
  QTest::newRow("right Shift") << quint64(VK_RSHIFT) << quint64(WindowsNativeKeyStateRightShift)
                               << quint64(allHeld & ~WindowsNativeKeyStateRightShift);
  QTest::newRow("left Control") << quint64(VK_LCONTROL) << quint64(WindowsNativeKeyStateLeftControl)
                                << quint64(allHeld & ~WindowsNativeKeyStateLeftControl);
  QTest::newRow("right Control") << quint64(VK_RCONTROL) << quint64(WindowsNativeKeyStateRightControl)
                                 << quint64(allHeld & ~WindowsNativeKeyStateRightControl);
  QTest::newRow("left Alt") << quint64(VK_LMENU) << quint64(WindowsNativeKeyStateLeftAlt)
                            << quint64(allHeld & ~WindowsNativeKeyStateLeftAlt);
  QTest::newRow("right Alt") << quint64(VK_RMENU) << quint64(WindowsNativeKeyStateRightAlt)
                             << quint64(allHeld & ~WindowsNativeKeyStateRightAlt);
  QTest::newRow("left Windows") << quint64(VK_LWIN) << quint64(WindowsNativeKeyStateLeftSuper)
                                << quint64(allHeld & ~WindowsNativeKeyStateLeftSuper);
  QTest::newRow("right Windows") << quint64(VK_RWIN) << quint64(WindowsNativeKeyStateRightSuper)
                                 << quint64(allHeld & ~WindowsNativeKeyStateRightSuper);
  QTest::newRow("regular key") << quint64('A') << quint64(0) << quint64(allHeld);
}

void MSWindowsHookTests::hookHeldKeyTransition()
{
  using namespace deskflow::platform;

  QFETCH(quint64, virtualKey);
  QFETCH(quint64, expectedDown);
  QFETCH(quint64, expectedUp);

  constexpr uint32_t allHeld = WindowsNativeKeyStateLeftShift | WindowsNativeKeyStateRightShift |
                               WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl |
                               WindowsNativeKeyStateLeftAlt | WindowsNativeKeyStateRightAlt |
                               WindowsNativeKeyStateLeftSuper | WindowsNativeKeyStateRightSuper;

  QCOMPARE(quint64(advanceWindowsHookHeldKeyState(0, static_cast<UINT>(virtualKey), true)), expectedDown);
  QCOMPARE(quint64(advanceWindowsHookHeldKeyState(allHeld, static_cast<UINT>(virtualKey), false)), expectedUp);
}

void MSWindowsHookTests::hookHeldKeySequence()
{
  using namespace deskflow::platform;

  uint32_t nativeState = 0;
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LSHIFT, true);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LCONTROL, true);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_RMENU, true);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LWIN, true);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, 'C', true);

  QCOMPARE(
      windowsModifierMaskFromNativeKeyState(nativeState),
      KeyModifierMask(KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierSuper)
  );

  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LSHIFT, false);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LCONTROL, false);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_RMENU, false);
  nativeState = advanceWindowsHookHeldKeyState(nativeState, VK_LWIN, false);

  QCOMPARE(windowsModifierMaskFromNativeKeyState(nativeState), KeyModifierMask(0));
}

void MSWindowsHookTests::characterModifierStateFollowsAcceptedHookState()
{
  using namespace deskflow::platform;

  BYTE keyState[256] = {};
  constexpr UINT modifierKeys[] = {VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_LCONTROL, VK_RCONTROL,
                                   VK_MENU,  VK_LMENU,  VK_RMENU,  VK_LWIN,    VK_RWIN};
  for (const auto virtualKey : modifierKeys) {
    keyState[virtualKey] = 0x80u;
  }
  keyState[VK_CAPITAL] = 0x81u;
  keyState['1'] = 0x80u;

  constexpr uint32_t acceptedState = WindowsNativeKeyStateRightShift | WindowsNativeKeyStateLeftControl |
                                     WindowsNativeKeyStateRightAlt | WindowsNativeKeyStateLeftSuper;
  synchronizeWindowsCharacterModifierState(keyState, acceptedState);

  QCOMPARE(keyState[VK_LSHIFT], BYTE(0));
  QCOMPARE(keyState[VK_RSHIFT], BYTE(0x80));
  QCOMPARE(keyState[VK_SHIFT], BYTE(0x80));
  QCOMPARE(keyState[VK_LCONTROL], BYTE(0x80));
  QCOMPARE(keyState[VK_RCONTROL], BYTE(0));
  QCOMPARE(keyState[VK_CONTROL], BYTE(0x80));
  QCOMPARE(keyState[VK_LMENU], BYTE(0));
  QCOMPARE(keyState[VK_RMENU], BYTE(0x80));
  QCOMPARE(keyState[VK_MENU], BYTE(0x80));
  QCOMPARE(keyState[VK_LWIN], BYTE(0x80));
  QCOMPARE(keyState[VK_RWIN], BYTE(0));
  QCOMPARE(keyState[VK_CAPITAL], BYTE(0x81));
  QCOMPARE(keyState['1'], BYTE(0x80));
}

void MSWindowsHookTests::staleAsyncShiftReleaseDoesNotReachCharacterTranslation()
{
  using namespace deskflow::platform;

  BYTE keyState[256] = {};
  keyState[VK_SHIFT] = 0x80u;
  keyState[VK_LSHIFT] = 0x80u;
  keyState['1'] = 0x80u;

  // Reproduce the incident boundary: GetAsyncKeyState still reports Shift
  // down after the accepted hook stream has processed its release.
  synchronizeWindowsCharacterModifierState(keyState, 0);

  QCOMPARE(keyState[VK_SHIFT], BYTE(0));
  QCOMPARE(keyState[VK_LSHIFT], BYTE(0));
  QCOMPARE(keyState[VK_RSHIFT], BYTE(0));
  QCOMPARE(keyState['1'], BYTE(0x80));
}

void MSWindowsHookTests::mouseKeyStateSnapshotPacking()
{
  using namespace deskflow::platform;

  constexpr uint32_t hookState = WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateNumLock;
  constexpr uint32_t observedState = WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightAlt;
  const auto snapshot = packWindowsHookKeyStateSnapshot(hookState, observedState);

  QCOMPARE(windowsHookKeyStateFromSnapshot(snapshot), hookState);
  QCOMPARE(windowsObservedKeyStateFromSnapshot(snapshot), observedState);
}

void MSWindowsHookTests::staleLocalModifierState_data()
{
  using namespace deskflow::platform;

  QTest::addColumn<quint64>("hookState");
  QTest::addColumn<quint64>("asyncState");
  QTest::addColumn<quint64>("observedState");
  QTest::addColumn<quint64>("expected");

  constexpr uint32_t modifierFlags[] = {WindowsNativeKeyStateLeftShift,   WindowsNativeKeyStateRightShift,
                                        WindowsNativeKeyStateLeftControl, WindowsNativeKeyStateRightControl,
                                        WindowsNativeKeyStateLeftAlt,     WindowsNativeKeyStateRightAlt,
                                        WindowsNativeKeyStateLeftSuper,   WindowsNativeKeyStateRightSuper};
  constexpr const char *modifierNames[] = {"left Shift", "right Shift", "left Control", "right Control",
                                           "left Alt",   "right Alt",   "left Windows", "right Windows"};

  for (std::size_t i = 0; i < std::size(modifierFlags); ++i) {
    QTest::newRow((std::string(modifierNames[i]) + " async-only stale").c_str())
        << quint64(0) << quint64(modifierFlags[i]) << quint64(modifierFlags[i]) << quint64(modifierFlags[i]);
    QTest::newRow((std::string(modifierNames[i]) + " physically held").c_str())
        << quint64(modifierFlags[i]) << quint64(modifierFlags[i]) << quint64(modifierFlags[i]) << quint64(0);
    QTest::newRow((std::string(modifierNames[i]) + " suppressed remote press").c_str())
        << quint64(modifierFlags[i]) << quint64(0) << quint64(modifierFlags[i]) << quint64(0);
  }

  QTest::newRow("all stale") << quint64(0) << quint64(kWindowsNonToggleNativeKeyState)
                             << quint64(kWindowsNonToggleNativeKeyState) << quint64(kWindowsNonToggleNativeKeyState);
  QTest::newRow("toggle state is ignored")
      << quint64(0)
      << quint64(WindowsNativeKeyStateCapsLock | WindowsNativeKeyStateNumLock | WindowsNativeKeyStateScrollLock)
      << quint64(kWindowsNonToggleNativeKeyState) << quint64(0);
  QTest::newRow("failed async query cannot release a held key")
      << quint64(WindowsNativeKeyStateLeftControl) << quint64(0) << quint64(WindowsNativeKeyStateLeftControl)
      << quint64(0);
  QTest::newRow("unobserved startup state cannot release a held key")
      << quint64(0) << quint64(WindowsNativeKeyStateLeftControl) << quint64(0) << quint64(0);
  QTest::newRow("synthetic input snapshot cannot release a held key")
      << quint64(0) << quint64(WindowsNativeKeyStateLeftControl) << quint64(0) << quint64(0);
  QTest::newRow("mixed left and right ownership")
      << quint64(WindowsNativeKeyStateRightControl)
      << quint64(WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl)
      << quint64(WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl)
      << quint64(WindowsNativeKeyStateLeftControl);
}

void MSWindowsHookTests::staleLocalModifierState()
{
  QFETCH(quint64, hookState);
  QFETCH(quint64, asyncState);
  QFETCH(quint64, observedState);
  QFETCH(quint64, expected);

  QCOMPARE(
      quint64(
          deskflow::platform::windowsStaleLocalModifierState(
              static_cast<uint32_t>(hookState), static_cast<uint32_t>(asyncState), static_cast<uint32_t>(observedState)
          )
      ),
      expected
  );
}

void MSWindowsHookTests::reconciledKeyReleaseRouting_data()
{
  using enum deskflow::platform::WindowsReconciledKeyReleaseRoute;

  QTest::addColumn<bool>("isOnScreen");
  QTest::addColumn<bool>("trackedForLocalRestore");
  QTest::addColumn<int>("expected");

  QTest::newRow("local screen needs no ownership repair") << true << false << static_cast<int>(NoAction);
  QTest::newRow("pre-switch key consumes local restore") << false << true << static_cast<int>(ConsumeLocalRestore);
  QTest::newRow("missed remote release is relayed") << false << false << static_cast<int>(RelayRemote);
  QTest::newRow("stale tracked key is consumed after return") << true << true << static_cast<int>(ConsumeLocalRestore);
}

void MSWindowsHookTests::reconciledKeyReleaseRouting()
{
  QFETCH(bool, isOnScreen);
  QFETCH(bool, trackedForLocalRestore);
  QFETCH(int, expected);

  QCOMPARE(
      static_cast<int>(deskflow::platform::windowsReconciledKeyReleaseRoute(isOnScreen, trackedForLocalRestore)),
      expected
  );
}

void MSWindowsHookTests::nativeModifierMask_data()
{
  using namespace deskflow::platform;

  QTest::addColumn<quint64>("nativeState");
  QTest::addColumn<quint64>("expected");

  QTest::newRow("left Shift") << quint64(WindowsNativeKeyStateLeftShift) << quint64(KeyModifierShift);
  QTest::newRow("right Shift") << quint64(WindowsNativeKeyStateRightShift) << quint64(KeyModifierShift);
  QTest::newRow("left Control") << quint64(WindowsNativeKeyStateLeftControl) << quint64(KeyModifierControl);
  QTest::newRow("right Control") << quint64(WindowsNativeKeyStateRightControl) << quint64(KeyModifierControl);
  QTest::newRow("left Alt") << quint64(WindowsNativeKeyStateLeftAlt) << quint64(KeyModifierAlt);
  QTest::newRow("right Alt") << quint64(WindowsNativeKeyStateRightAlt) << quint64(KeyModifierAlt);
  QTest::newRow("left Windows") << quint64(WindowsNativeKeyStateLeftSuper) << quint64(KeyModifierSuper);
  QTest::newRow("right Windows") << quint64(WindowsNativeKeyStateRightSuper) << quint64(KeyModifierSuper);
  QTest::newRow("Caps Lock") << quint64(WindowsNativeKeyStateCapsLock) << quint64(KeyModifierCapsLock);
  QTest::newRow("Num Lock") << quint64(WindowsNativeKeyStateNumLock) << quint64(KeyModifierNumLock);
  QTest::newRow("Scroll Lock") << quint64(WindowsNativeKeyStateScrollLock) << quint64(KeyModifierScrollLock);
  QTest::newRow("none") << quint64(0) << quint64(0);
  QTest::newRow("all") << quint64(
                              WindowsNativeKeyStateLeftShift | WindowsNativeKeyStateRightShift |
                              WindowsNativeKeyStateLeftControl | WindowsNativeKeyStateRightControl |
                              WindowsNativeKeyStateLeftAlt | WindowsNativeKeyStateRightAlt |
                              WindowsNativeKeyStateLeftSuper | WindowsNativeKeyStateRightSuper |
                              WindowsNativeKeyStateCapsLock | WindowsNativeKeyStateNumLock |
                              WindowsNativeKeyStateScrollLock
                          )
                       << quint64(
                              KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierSuper |
                              KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock
                          );
}

void MSWindowsHookTests::nativeModifierMask()
{
  QFETCH(quint64, nativeState);
  QFETCH(quint64, expected);

  QCOMPARE(
      quint64(deskflow::platform::windowsModifierMaskFromNativeKeyState(static_cast<uint32_t>(nativeState))), expected
  );
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
