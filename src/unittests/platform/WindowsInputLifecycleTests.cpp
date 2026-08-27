/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerKeyTranslator.h"
#include "platform/MSWindowsKeyEventPolicy.h"

#include <QTest>

#include <array>
#include <map>
#include <random>
#include <set>
#include <string>

namespace {

using deskflow::platform::WindowsPrimaryKeyRestoreRoute;

struct ModifierSpec
{
  const char *name;
  UINT virtualKey;
  KeyID sourceKey;
  KeyID targetKey;
  KeyButton button;
  uint32_t nativeFlag;
  KeyModifierMask sourceMask;
  KeyModifierMask targetMask;
};

constexpr std::array<ModifierSpec, 8> kModifierSpecs{{
    {"left Shift", VK_LSHIFT, kKeyShift_L, kKeyShift_L, 0x02a, deskflow::platform::WindowsNativeKeyStateLeftShift,
     KeyModifierShift, KeyModifierShift},
    {"right Shift", VK_RSHIFT, kKeyShift_R, kKeyShift_R, 0x036, deskflow::platform::WindowsNativeKeyStateRightShift,
     KeyModifierShift, KeyModifierShift},
    {"left Control", VK_LCONTROL, kKeyControl_L, kKeySuper_L, 0x01d,
     deskflow::platform::WindowsNativeKeyStateLeftControl, KeyModifierControl, KeyModifierSuper},
    {"right Control", VK_RCONTROL, kKeyControl_R, kKeySuper_R, 0x11d,
     deskflow::platform::WindowsNativeKeyStateRightControl, KeyModifierControl, KeyModifierSuper},
    {"left Alt", VK_LMENU, kKeyAlt_L, kKeyAlt_L, 0x038, deskflow::platform::WindowsNativeKeyStateLeftAlt,
     KeyModifierAlt, KeyModifierAlt},
    {"right Alt", VK_RMENU, kKeyAltGr, kKeyAltGr, 0x138, deskflow::platform::WindowsNativeKeyStateRightAlt,
     KeyModifierAlt, KeyModifierAlt},
    {"left Super", VK_LWIN, kKeySuper_L, kKeyControl_L, 0x15b, deskflow::platform::WindowsNativeKeyStateLeftSuper,
     KeyModifierSuper, KeyModifierControl},
    {"right Super", VK_RWIN, kKeySuper_R, kKeyControl_R, 0x15c, deskflow::platform::WindowsNativeKeyStateRightSuper,
     KeyModifierSuper, KeyModifierControl},
}};

constexpr uint32_t kNonToggleNativeState =
    deskflow::platform::WindowsNativeKeyStateLeftShift | deskflow::platform::WindowsNativeKeyStateRightShift |
    deskflow::platform::WindowsNativeKeyStateLeftControl | deskflow::platform::WindowsNativeKeyStateRightControl |
    deskflow::platform::WindowsNativeKeyStateLeftAlt | deskflow::platform::WindowsNativeKeyStateRightAlt |
    deskflow::platform::WindowsNativeKeyStateLeftSuper | deskflow::platform::WindowsNativeKeyStateRightSuper;

struct RoutedKeyEvent
{
  WindowsPrimaryKeyRestoreRoute restoreRoute{WindowsPrimaryKeyRestoreRoute::Relay};
  bool relayed{false};
  KeyID targetKey{kKeyNone};
  KeyModifierMask targetMask{0};
};

struct MouseSnapshotResult
{
  std::set<KeyButton> consumedLocalRestores;
  std::set<KeyButton> relayedRemoteReleases;
  std::set<KeyButton> repairedLocalModifiers;
};

class WindowsInputLifecycle
{
public:
  WindowsInputLifecycle()
  {
    m_translator.mapModifier(kKeyModifierIDControl, kKeyModifierIDSuper);
    m_translator.mapModifier(kKeyModifierIDSuper, kKeyModifierIDControl);
    m_translator.mapModifier(kKeyModifierIDMeta, kKeyModifierIDControl);
  }

  RoutedKeyEvent keyEvent(const ModifierSpec &key, bool down)
  {
    const bool wasDown = m_sourceDown.contains(key.button);
    m_hookHeld = deskflow::platform::advanceWindowsHookHeldKeyState(m_hookHeld, key.virtualKey, down);
    if (down) {
      m_sourceDown.insert(key.button);
      m_completedSourceUps.erase(key.button);
    } else {
      m_sourceDown.erase(key.button);
      m_completedSourceUps.insert(key.button);
    }

    RoutedKeyEvent result;
    result.restoreRoute = deskflow::platform::windowsPrimaryKeyRestoreRoute(
        m_onScreen, down, wasDown, m_localRestore.contains(key.button)
    );
    if (result.restoreRoute == WindowsPrimaryKeyRestoreRoute::ForgetStaleRestore) {
      m_localRestore.erase(key.button);
    } else if (result.restoreRoute == WindowsPrimaryKeyRestoreRoute::RestoreLocally) {
      m_localRestore.erase(key.button);
      ++m_localRestoreCount;
      return result;
    }

    if (m_onScreen || (down && wasDown)) {
      return result;
    }

    result.relayed = true;
    result.targetKey = m_translator.translateKey(key.sourceKey);
    result.targetMask = m_translator.translateModifierMask(
        deskflow::platform::windowsModifierMaskFromNativeKeyState(m_hookHeld | m_toggleState)
    );
    if (down) {
      m_remoteHeld[key.button] = result.targetKey;
    } else {
      m_remoteHeld.erase(key.button);
    }
    return result;
  }

  RoutedKeyEvent ordinaryKeyEvent(KeyID sourceKey) const
  {
    RoutedKeyEvent result;
    if (m_onScreen) {
      return result;
    }

    result.relayed = true;
    result.targetKey = m_translator.translateKey(sourceKey);
    result.targetMask = translatedModifierMask();
    return result;
  }

  KeyModifierMask translatedModifierMask() const
  {
    return m_translator.translateModifierMask(
        deskflow::platform::windowsModifierMaskFromNativeKeyState(m_hookHeld | m_toggleState)
    );
  }

  KeyModifierMask mouseWheelModifierMask() const
  {
    return translatedModifierMask();
  }

  void leavePrimary()
  {
    m_localRestore = m_sourceDown;
    m_onScreen = false;
  }

  void enterPrimary()
  {
    m_remoteHeld.clear();
    m_localRestore.clear();
    m_completedSourceUps.clear();
    m_onScreen = true;
  }

  void disconnectRemote()
  {
    enterPrimary();
  }

  void reconcileNativeState(uint32_t nativeState, uint32_t observedState = kNonToggleNativeState)
  {
    const auto observedModifiers = observedState & kNonToggleNativeState;
    m_hookHeld = (m_hookHeld & ~observedModifiers) | (nativeState & observedModifiers);
    m_toggleState = nativeState & ~kNonToggleNativeState;
    for (const auto &key : kModifierSpecs) {
      if ((observedModifiers & key.nativeFlag) == 0u) {
        continue;
      }
      if ((nativeState & key.nativeFlag) != 0u) {
        m_sourceDown.insert(key.button);
      } else {
        m_sourceDown.erase(key.button);
      }
    }
  }

  void reconcileMouseSnapshot()
  {
    reconcileCurrentWindowsState(m_hookHeld | m_toggleState, m_hookHeld, kNonToggleNativeState);
  }

  MouseSnapshotResult
  reconcileCurrentWindowsState(uint32_t hookState, uint32_t asyncState, uint32_t observedState = kNonToggleNativeState)
  {
    const auto sourceDownBefore = m_sourceDown;
    reconcileNativeState(hookState, observedState);

    MouseSnapshotResult result;
    for (const auto &key : kModifierSpecs) {
      if (!sourceDownBefore.contains(key.button) || m_sourceDown.contains(key.button)) {
        continue;
      }

      const bool trackedForLocalRestore = m_localRestore.contains(key.button);
      const auto route = deskflow::platform::windowsReconciledKeyReleaseRoute(m_onScreen, trackedForLocalRestore);
      if (route == deskflow::platform::WindowsReconciledKeyReleaseRoute::ConsumeLocalRestore) {
        m_localRestore.erase(key.button);
        result.consumedLocalRestores.insert(key.button);
      } else if (route == deskflow::platform::WindowsReconciledKeyReleaseRoute::RelayRemote) {
        m_remoteHeld.erase(key.button);
        result.relayedRemoteReleases.insert(key.button);
      }
    }

    const auto staleLocalModifiers =
        deskflow::platform::windowsStaleLocalModifierState(hookState, asyncState, observedState);
    for (const auto &key : kModifierSpecs) {
      if ((staleLocalModifiers & key.nativeFlag) != 0u) {
        result.repairedLocalModifiers.insert(key.button);
      }
    }
    return result;
  }

  bool invariantsHold() const
  {
    if (m_onScreen && (!m_remoteHeld.empty() || !m_localRestore.empty())) {
      return false;
    }
    for (const auto &[button, targetKey] : m_remoteHeld) {
      if (m_localRestore.contains(button) || m_completedSourceUps.contains(button) || targetKey == kKeyNone) {
        return false;
      }
    }
    return true;
  }

  bool isOnScreen() const
  {
    return m_onScreen;
  }

  bool sourceDown(KeyButton button) const
  {
    return m_sourceDown.contains(button);
  }

  bool localRestoreTracked(KeyButton button) const
  {
    return m_localRestore.contains(button);
  }

  bool remoteHeld(KeyButton button) const
  {
    return m_remoteHeld.contains(button);
  }

  KeyID remoteKey(KeyButton button) const
  {
    const auto found = m_remoteHeld.find(button);
    return found == m_remoteHeld.end() ? kKeyNone : found->second;
  }

  std::size_t remoteHeldCount() const
  {
    return m_remoteHeld.size();
  }

  std::size_t localRestoreCount() const
  {
    return m_localRestoreCount;
  }

private:
  ServerKeyTranslator m_translator;
  bool m_onScreen{true};
  uint32_t m_hookHeld{0};
  uint32_t m_toggleState{0};
  std::set<KeyButton> m_sourceDown;
  std::set<KeyButton> m_localRestore;
  std::set<KeyButton> m_completedSourceUps;
  std::map<KeyButton, KeyID> m_remoteHeld;
  std::size_t m_localRestoreCount{0};
};

} // namespace

class WindowsInputLifecycleTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void freshRemoteModifierLifecycle_data();
  void freshRemoteModifierLifecycle();
  void preSwitchHeldModifierRestoresLocally_data();
  void preSwitchHeldModifierRestoresLocally();
  void ordinaryInputDoesNotInheritReleasedModifier_data();
  void ordinaryInputDoesNotInheritReleasedModifier();
  void mixedLeftRightModifierOwnership();
  void screenReturnAndDisconnectReleaseRemoteKeys();
  void stalePreSwitchModifierUsesCurrentWindowsState_data();
  void stalePreSwitchModifierUsesCurrentWindowsState();
  void missedRemoteModifierReleaseIsRelayed_data();
  void missedRemoteModifierReleaseIsRelayed();
  void currentSnapshotPreservesPhysicallyHeldModifier_data();
  void currentSnapshotPreservesPhysicallyHeldModifier();
  void unobservedCurrentModifierIsNotRepaired();
  void currentSnapshotRepairsOnlyStaleSide();
  void deterministicStateMachineMaintainsOwnership();
};

void WindowsInputLifecycleTests::freshRemoteModifierLifecycle_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    QTest::newRow(kModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void WindowsInputLifecycleTests::freshRemoteModifierLifecycle()
{
  QFETCH(int, modifierIndex);
  const auto &key = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(key, true);
  lifecycle.leavePrimary();
  lifecycle.reconcileNativeState(0);

  const auto down = lifecycle.keyEvent(key, true);
  QCOMPARE(down.restoreRoute, WindowsPrimaryKeyRestoreRoute::ForgetStaleRestore);
  QVERIFY(down.relayed);
  QCOMPARE(down.targetKey, key.targetKey);
  QCOMPARE(down.targetMask, key.targetMask);
  QVERIFY(!lifecycle.localRestoreTracked(key.button));
  QVERIFY(lifecycle.remoteHeld(key.button));

  const auto repeat = lifecycle.keyEvent(key, true);
  QCOMPARE(repeat.restoreRoute, WindowsPrimaryKeyRestoreRoute::Relay);
  QVERIFY(!repeat.relayed);
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{1});

  lifecycle.reconcileMouseSnapshot();
  QVERIFY(lifecycle.sourceDown(key.button));
  QVERIFY(lifecycle.remoteHeld(key.button));

  const auto up = lifecycle.keyEvent(key, false);
  QCOMPARE(up.restoreRoute, WindowsPrimaryKeyRestoreRoute::Relay);
  QVERIFY(up.relayed);
  QCOMPARE(up.targetKey, key.targetKey);
  QCOMPARE(up.targetMask, static_cast<KeyModifierMask>(0));
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{0});
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::preSwitchHeldModifierRestoresLocally_data()
{
  QTest::addColumn<int>("modifierIndex");
  QTest::addColumn<bool>("reconcileBeforeRelease");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    const auto &key = kModifierSpecs[i];
    QTest::newRow((std::string(key.name) + " direct release").c_str()) << static_cast<int>(i) << false;
    QTest::newRow((std::string(key.name) + " reconciled release").c_str()) << static_cast<int>(i) << true;
  }
}

void WindowsInputLifecycleTests::preSwitchHeldModifierRestoresLocally()
{
  QFETCH(int, modifierIndex);
  QFETCH(bool, reconcileBeforeRelease);
  const auto &key = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(key, true);
  lifecycle.leavePrimary();
  if (reconcileBeforeRelease) {
    lifecycle.reconcileNativeState(0);
  }

  const auto up = lifecycle.keyEvent(key, false);
  QCOMPARE(up.restoreRoute, WindowsPrimaryKeyRestoreRoute::RestoreLocally);
  QVERIFY(!up.relayed);
  QCOMPARE(lifecycle.localRestoreCount(), std::size_t{1});
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{0});
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::ordinaryInputDoesNotInheritReleasedModifier_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    QTest::newRow(kModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void WindowsInputLifecycleTests::ordinaryInputDoesNotInheritReleasedModifier()
{
  QFETCH(int, modifierIndex);
  const auto &modifier = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(modifier, true);
  lifecycle.leavePrimary();
  lifecycle.reconcileNativeState(0);
  lifecycle.keyEvent(modifier, false);

  const auto fKey = lifecycle.ordinaryKeyEvent(static_cast<KeyID>('f'));
  QVERIFY(fKey.relayed);
  QCOMPARE(fKey.targetKey, static_cast<KeyID>('f'));
  QCOMPARE(fKey.targetMask, static_cast<KeyModifierMask>(0));

  const auto escapeKey = lifecycle.ordinaryKeyEvent(kKeyEscape);
  QVERIFY(escapeKey.relayed);
  QCOMPARE(escapeKey.targetKey, kKeyEscape);
  QCOMPARE(escapeKey.targetMask, static_cast<KeyModifierMask>(0));

  QCOMPARE(lifecycle.translatedModifierMask(), static_cast<KeyModifierMask>(0));
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::mixedLeftRightModifierOwnership()
{
  const auto &leftControl = kModifierSpecs[2];
  const auto &rightControl = kModifierSpecs[3];
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(leftControl, true);
  lifecycle.leavePrimary();
  lifecycle.reconcileNativeState(0);

  const auto rightDown = lifecycle.keyEvent(rightControl, true);
  QVERIFY(rightDown.relayed);
  QCOMPARE(lifecycle.remoteKey(rightControl.button), kKeySuper_R);
  QVERIFY(lifecycle.localRestoreTracked(leftControl.button));

  const auto leftUp = lifecycle.keyEvent(leftControl, false);
  QCOMPARE(leftUp.restoreRoute, WindowsPrimaryKeyRestoreRoute::RestoreLocally);
  QVERIFY(lifecycle.remoteHeld(rightControl.button));

  lifecycle.reconcileMouseSnapshot();
  const auto rightUp = lifecycle.keyEvent(rightControl, false);
  QVERIFY(rightUp.relayed);
  QCOMPARE(rightUp.targetKey, kKeySuper_R);
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{0});
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::screenReturnAndDisconnectReleaseRemoteKeys()
{
  const auto &control = kModifierSpecs[2];
  const auto &super = kModifierSpecs[6];
  WindowsInputLifecycle lifecycle;

  lifecycle.leavePrimary();
  lifecycle.keyEvent(control, true);
  lifecycle.keyEvent(super, true);
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{2});

  lifecycle.enterPrimary();
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{0});
  lifecycle.keyEvent(control, false);
  lifecycle.keyEvent(super, false);

  lifecycle.leavePrimary();
  lifecycle.keyEvent(control, true);
  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{1});
  lifecycle.disconnectRemote();

  QCOMPARE(lifecycle.remoteHeldCount(), std::size_t{0});
  QVERIFY(lifecycle.isOnScreen());
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::stalePreSwitchModifierUsesCurrentWindowsState_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    QTest::newRow(kModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void WindowsInputLifecycleTests::stalePreSwitchModifierUsesCurrentWindowsState()
{
  QFETCH(int, modifierIndex);
  const auto &modifier = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(modifier, true);
  lifecycle.leavePrimary();

  // The hook has observed the physical release, while Windows still reports
  // the modifier down because relay mode suppressed that release.
  const auto snapshot = lifecycle.reconcileCurrentWindowsState(0, modifier.nativeFlag);

  QVERIFY(snapshot.consumedLocalRestores.contains(modifier.button));
  QVERIFY(snapshot.repairedLocalModifiers.contains(modifier.button));
  QVERIFY(snapshot.relayedRemoteReleases.empty());
  QVERIFY(!lifecycle.sourceDown(modifier.button));
  QVERIFY(!lifecycle.localRestoreTracked(modifier.button));

  const auto fKey = lifecycle.ordinaryKeyEvent(static_cast<KeyID>('f'));
  QCOMPARE(fKey.targetMask, static_cast<KeyModifierMask>(0));
  const auto escapeKey = lifecycle.ordinaryKeyEvent(kKeyEscape);
  QCOMPARE(escapeKey.targetMask, static_cast<KeyModifierMask>(0));
  QCOMPARE(lifecycle.mouseWheelModifierMask(), static_cast<KeyModifierMask>(0));
  QCOMPARE(lifecycle.translatedModifierMask(), static_cast<KeyModifierMask>(0));
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::missedRemoteModifierReleaseIsRelayed_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    QTest::newRow(kModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void WindowsInputLifecycleTests::missedRemoteModifierReleaseIsRelayed()
{
  QFETCH(int, modifierIndex);
  const auto &modifier = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.leavePrimary();
  lifecycle.keyEvent(modifier, true);
  QVERIFY(lifecycle.remoteHeld(modifier.button));

  // Simulate a dropped queued key-up message. The following mouse snapshot
  // still carries the hook's accepted physical state and repairs ownership.
  const auto snapshot = lifecycle.reconcileCurrentWindowsState(0, 0);

  QVERIFY(snapshot.relayedRemoteReleases.contains(modifier.button));
  QVERIFY(snapshot.repairedLocalModifiers.empty());
  QVERIFY(!lifecycle.remoteHeld(modifier.button));
  QCOMPARE(lifecycle.translatedModifierMask(), static_cast<KeyModifierMask>(0));
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::currentSnapshotPreservesPhysicallyHeldModifier_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kModifierSpecs.size(); ++i) {
    QTest::newRow(kModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void WindowsInputLifecycleTests::currentSnapshotPreservesPhysicallyHeldModifier()
{
  QFETCH(int, modifierIndex);
  const auto &modifier = kModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(modifier, true);
  lifecycle.leavePrimary();

  const auto snapshot = lifecycle.reconcileCurrentWindowsState(modifier.nativeFlag, modifier.nativeFlag);

  QVERIFY(snapshot.consumedLocalRestores.empty());
  QVERIFY(snapshot.relayedRemoteReleases.empty());
  QVERIFY(snapshot.repairedLocalModifiers.empty());
  QVERIFY(lifecycle.sourceDown(modifier.button));
  QVERIFY(lifecycle.localRestoreTracked(modifier.button));
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::unobservedCurrentModifierIsNotRepaired()
{
  const auto &leftControl = kModifierSpecs[2];
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(leftControl, true);
  lifecycle.leavePrimary();
  const auto snapshot = lifecycle.reconcileCurrentWindowsState(0, leftControl.nativeFlag, 0);

  QVERIFY(snapshot.repairedLocalModifiers.empty());
  QVERIFY(lifecycle.sourceDown(leftControl.button));
  QVERIFY(lifecycle.localRestoreTracked(leftControl.button));
  QCOMPARE(lifecycle.translatedModifierMask(), leftControl.targetMask);
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::currentSnapshotRepairsOnlyStaleSide()
{
  const auto &leftControl = kModifierSpecs[2];
  const auto &rightControl = kModifierSpecs[3];
  WindowsInputLifecycle lifecycle;

  lifecycle.keyEvent(rightControl, true);
  lifecycle.leavePrimary();

  const auto snapshot =
      lifecycle.reconcileCurrentWindowsState(rightControl.nativeFlag, leftControl.nativeFlag | rightControl.nativeFlag);

  QVERIFY(snapshot.repairedLocalModifiers == std::set<KeyButton>{leftControl.button});
  QVERIFY(lifecycle.sourceDown(rightControl.button));
  QCOMPARE(lifecycle.translatedModifierMask(), rightControl.targetMask);
  QVERIFY(lifecycle.invariantsHold());
}

void WindowsInputLifecycleTests::deterministicStateMachineMaintainsOwnership()
{
  std::mt19937 random(0x5eedc0deu);
  WindowsInputLifecycle lifecycle;

  for (int step = 0; step < 4096; ++step) {
    const auto &key = kModifierSpecs[random() % kModifierSpecs.size()];
    switch (random() % 8u) {
    case 0:
      lifecycle.keyEvent(key, true);
      break;
    case 1:
      lifecycle.keyEvent(key, false);
      break;
    case 2:
      if (lifecycle.isOnScreen()) {
        lifecycle.leavePrimary();
      }
      break;
    case 3:
      if (!lifecycle.isOnScreen()) {
        lifecycle.enterPrimary();
      }
      break;
    case 4:
      lifecycle.reconcileMouseSnapshot();
      break;
    case 5:
      lifecycle.reconcileNativeState(0);
      break;
    case 6:
      if (!lifecycle.isOnScreen()) {
        lifecycle.disconnectRemote();
      }
      break;
    case 7:
      lifecycle.reconcileNativeState(
          deskflow::platform::WindowsNativeKeyStateCapsLock | deskflow::platform::WindowsNativeKeyStateNumLock |
          (lifecycle.sourceDown(key.button) ? key.nativeFlag : 0u)
      );
      break;
    }

    QVERIFY2(lifecycle.invariantsHold(), qPrintable(QString("input ownership invariant failed at step %1").arg(step)));
  }

  lifecycle.enterPrimary();
  QVERIFY(lifecycle.invariantsHold());
}

QTEST_MAIN(WindowsInputLifecycleTests)

#include "WindowsInputLifecycleTests.moc"
