/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "arch/Arch.h"
#include "base/EventQueue.h"
#include "base/Log.h"
#include "platform/MSWindowsKeyEventPolicy.h"
#include "platform/MSWindowsKeyState.h"

#include <QTest>

class MSWindowsKeyStateTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void initTestCase();
  void reconciliationReturnsReleasedModifier_data();
  void reconciliationReturnsReleasedModifier();
  void unobservedModifierIsPreserved();
  void secondaryIgnoresPrimaryNativeState();

private:
  Arch m_arch;
  Log m_log;
};

void MSWindowsKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void MSWindowsKeyStateTests::reconciliationReturnsReleasedModifier_data()
{
  using namespace deskflow::platform;

  QTest::addColumn<quint64>("virtualKey");
  QTest::addColumn<quint64>("nativeFlag");

  QTest::newRow("left Shift") << quint64(VK_LSHIFT) << quint64(WindowsNativeKeyStateLeftShift);
  QTest::newRow("right Shift") << quint64(VK_RSHIFT) << quint64(WindowsNativeKeyStateRightShift);
  QTest::newRow("left Control") << quint64(VK_LCONTROL) << quint64(WindowsNativeKeyStateLeftControl);
  QTest::newRow("right Control") << quint64(VK_RCONTROL) << quint64(WindowsNativeKeyStateRightControl);
  QTest::newRow("left Alt") << quint64(VK_LMENU) << quint64(WindowsNativeKeyStateLeftAlt);
  QTest::newRow("right Alt") << quint64(VK_RMENU) << quint64(WindowsNativeKeyStateRightAlt);
  QTest::newRow("left Windows") << quint64(VK_LWIN) << quint64(WindowsNativeKeyStateLeftSuper);
  QTest::newRow("right Windows") << quint64(VK_RWIN) << quint64(WindowsNativeKeyStateRightSuper);
}

void MSWindowsKeyStateTests::reconciliationReturnsReleasedModifier()
{
  QFETCH(quint64, virtualKey);
  QFETCH(quint64, nativeFlag);

  EventQueue eventQueue;
  MSWindowsKeyState keyState(nullptr, nullptr, &eventQueue, {"en"}, true, true);
  keyState.updateKeyMap();

  const auto key = static_cast<UINT>(virtualKey);
  const auto button = keyState.virtualKeyToButton(key);
  QVERIFY(button != 0);

  keyState.applyKeyEvent(button, key, true);
  QVERIFY(keyState.isKeyDown(button));
  QVERIFY(keyState.reconcileNativeKeyState(static_cast<uint32_t>(nativeFlag), static_cast<uint32_t>(nativeFlag)).empty()
  );

  const auto released = keyState.reconcileNativeKeyState(0, static_cast<uint32_t>(nativeFlag));
  QVERIFY(released == IKeyState::KeyButtonSet{button});
  QVERIFY(!keyState.isKeyDown(button));
}

void MSWindowsKeyStateTests::secondaryIgnoresPrimaryNativeState()
{
  EventQueue eventQueue;
  MSWindowsKeyState keyState(nullptr, nullptr, &eventQueue, {"en"}, true, false);
  keyState.updateKeyMap();

  const auto button = keyState.virtualKeyToButton(VK_LCONTROL);
  QVERIFY(button != 0);
  keyState.applyKeyEvent(button, VK_LCONTROL, true);

  QVERIFY(keyState.reconcileNativeKeyState(0, deskflow::platform::WindowsNativeKeyStateLeftControl).empty());
  QVERIFY(keyState.isKeyDown(button));
}

void MSWindowsKeyStateTests::unobservedModifierIsPreserved()
{
  EventQueue eventQueue;
  MSWindowsKeyState keyState(nullptr, nullptr, &eventQueue, {"en"}, true, true);
  keyState.updateKeyMap();

  const auto button = keyState.virtualKeyToButton(VK_LCONTROL);
  QVERIFY(button != 0);
  keyState.applyKeyEvent(button, VK_LCONTROL, true);

  QVERIFY(keyState.reconcileNativeKeyState(0, 0).empty());
  QVERIFY(keyState.isKeyDown(button));
}

QTEST_MAIN(MSWindowsKeyStateTests)

#include "MSWindowsKeyStateTests.moc"
