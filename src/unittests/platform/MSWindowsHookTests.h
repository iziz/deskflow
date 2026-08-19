/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <QTest>

class MSWindowsHookTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void advanceToggleKeyState_data();
  void advanceToggleKeyState();
  void virtualKeyHotKeyRouting_data();
  void virtualKeyHotKeyRouting();
  void toggleKeyTransitionSequence();
  void nativeToggleStateReconciliation();
  void inputDesktopToggleSynchronization();
  void windowsHotKeyRegistration_data();
  void windowsHotKeyRegistration();
  void relaySuppression_data();
  void relaySuppression();
  void preModeMouseEvent_data();
  void preModeMouseEvent();
};
