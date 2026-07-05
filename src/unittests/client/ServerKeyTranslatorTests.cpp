/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerKeyTranslator.h"
#include "base/Log.h"

#include <QTest>

class ServerKeyTranslatorTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void translateKeyPreservesNonModifierKeys();
  void translateModifierMaskPreservesBitmaskValues();
  void translateModifierMapClampsIndexOnly();
  void resetRestoresIdentityMapping();

private:
  Log m_log;
};

void ServerKeyTranslatorTests::translateKeyPreservesNonModifierKeys()
{
  ServerKeyTranslator translator;

  QCOMPARE(translator.translateKey(static_cast<KeyID>('a')), static_cast<KeyID>('a'));
  QCOMPARE(translator.translateKey(kKeyF1), kKeyF1);
}

void ServerKeyTranslatorTests::translateModifierMaskPreservesBitmaskValues()
{
  ServerKeyTranslator translator;

  const auto mask = KeyModifierSuper | KeyModifierCapsLock | KeyModifierNumLock;

  QCOMPARE(translator.translateModifierMask(mask), mask);
}

void ServerKeyTranslatorTests::translateModifierMapClampsIndexOnly()
{
  ServerKeyTranslator translator;

  translator.mapModifier(kKeyModifierIDAlt, kKeyModifierIDSuper);
  translator.mapModifier(kKeyModifierIDSuper, 999);

  QCOMPARE(translator.translateKey(kKeyAlt_R), kKeySuper_R);
  QCOMPARE(translator.translateKey(kKeySuper_L), kKeyAltGr);
  QCOMPARE(
      translator.translateModifierMask(KeyModifierAlt | KeyModifierSuper | KeyModifierCapsLock),
      KeyModifierSuper | KeyModifierAltGr | KeyModifierCapsLock
  );
}

void ServerKeyTranslatorTests::resetRestoresIdentityMapping()
{
  ServerKeyTranslator translator;

  translator.mapModifier(kKeyModifierIDAlt, kKeyModifierIDSuper);
  translator.reset();

  QCOMPARE(translator.translateKey(kKeyAlt_R), kKeyAlt_R);
  QCOMPARE(translator.translateModifierMask(KeyModifierAlt | KeyModifierCapsLock), KeyModifierAlt | KeyModifierCapsLock);
}

QTEST_MAIN(ServerKeyTranslatorTests)

#include "ServerKeyTranslatorTests.moc"
