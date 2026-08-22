/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "base/Log.h"

#include "arch/Arch.h"
#include "platform/OSXKeyState.h"

#include <QTest>

class OSXKeyStateTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void initTestCase();
  // Test are run in order top to bottom
  void mapModifiersFromOSX_OSXMask();
  void mapKeyFromEventUsesEventModifierFlags();
  void syncModifiersFromOSX_releasesStaleSuper();
  void syncModifiersFromOSX_ignoresNumericPadFlag();
  void syncToggleModifiers_ignoresRemoteNumLock();
  void syncModifiersFromOSX_clearsStaleShadowWhenMaskUnchanged();
  void clearStaleModifiers_refreshesShadowFromSystemState();
  void clearStaleModifiers_releasesSyntheticModifiersMissingFromSystemState();
  void clearStaleModifiers_releasesPreviouslyPostedModifierAfterMatchingUp();
  void nativeKeyTransaction_appliesAuthoritativeFlagsToEveryKey();
  void nativeKeyTransaction_preservesRightModifierSide();
  void nativeKeyTransaction_rollsBackModifierAfterPostFailure();

private:
  Arch m_arch;
  Log m_log;
};
