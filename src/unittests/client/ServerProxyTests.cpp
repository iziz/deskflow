/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/AppUtil.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ClipboardTransfer.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/KeyboardLayoutManager.h"
#include "io/IStream.h"

#include <QTest>

#include <cstdint>

#define private public
#include "client/ServerProxy.h"
#undef private

namespace {

class TestAppUtil : public AppUtil
{
public:
  int run() override
  {
    return 0;
  }

  void startNode() override
  {
  }

  std::vector<std::string> getKeyboardLayoutList() override
  {
    return {};
  }

  std::string getCurrentLanguageCode() override
  {
    return {};
  }
};

class TestStream : public deskflow::IStream
{
public:
  void close() override
  {
  }

  uint32_t read(void *, uint32_t) override
  {
    return 0;
  }

  void write(const void *, uint32_t) override
  {
  }

  void flush() override
  {
  }

  void shutdownInput() override
  {
  }

  void shutdownOutput() override
  {
  }

  void *getEventTarget() const override
  {
    return const_cast<TestStream *>(this);
  }

  bool isReady() const override
  {
    return false;
  }

  uint32_t getSize() const override
  {
    return 0;
  }
};

class TestEventQueue : public IEventQueue
{
public:
  int loop() override
  {
    return 0;
  }

  void adoptBuffer(IEventQueueBuffer *) override
  {
  }

  bool getEvent(Event &, double) override
  {
    return false;
  }

  bool dispatchEvent(const Event &) override
  {
    return false;
  }

  void addEvent(Event &&) override
  {
  }

  EventQueueTimer *newTimer(double, void *) override
  {
    return nullptr;
  }

  EventQueueTimer *newOneShotTimer(double, void *) override
  {
    return nullptr;
  }

  void deleteTimer(EventQueueTimer *) override
  {
  }

  void addHandler(EventTypes, void *, const EventHandler &) override
  {
  }

  void removeHandler(EventTypes, void *) override
  {
  }

  void removeHandlers(void *) override
  {
  }

  void waitForReady() const override
  {
  }

  void *getSystemTarget() override
  {
    return this;
  }
};

class ServerProxyFixture
{
public:
  ServerProxyFixture() : proxy(reinterpret_cast<Client *>(0x1), &stream, &events, false)
  {
  }

  TestAppUtil appUtil;
  TestStream stream;
  TestEventQueue events;
  ServerProxy proxy;
};

} // namespace

class ServerProxyTests : public QObject
{
  Q_OBJECT

private Q_SLOTS:
  void translateKeyPreservesNonModifierKeys();
  void translateModifierMaskPreservesBitmaskValues();
  void translateModifierMapClampsIndexOnly();

private:
  Log m_log;
};

void ServerProxyTests::translateKeyPreservesNonModifierKeys()
{
  ServerProxyFixture fixture;

  QCOMPARE(fixture.proxy.translateKey(static_cast<KeyID>('a')), static_cast<KeyID>('a'));
  QCOMPARE(fixture.proxy.translateKey(kKeyF1), kKeyF1);
}

void ServerProxyTests::translateModifierMaskPreservesBitmaskValues()
{
  ServerProxyFixture fixture;

  const auto mask = KeyModifierSuper | KeyModifierCapsLock | KeyModifierNumLock;

  QCOMPARE(fixture.proxy.translateModifierMask(mask), mask);
}

void ServerProxyTests::translateModifierMapClampsIndexOnly()
{
  ServerProxyFixture fixture;

  fixture.proxy.m_modifierTranslationTable[kKeyModifierIDAlt] = kKeyModifierIDSuper;
  fixture.proxy.m_modifierTranslationTable[kKeyModifierIDSuper] = 999;

  QCOMPARE(fixture.proxy.translateKey(kKeyAlt_R), kKeySuper_R);
  QCOMPARE(fixture.proxy.translateKey(kKeySuper_L), kKeyAltGr);
  QCOMPARE(
      fixture.proxy.translateModifierMask(KeyModifierAlt | KeyModifierSuper | KeyModifierCapsLock),
      KeyModifierSuper | KeyModifierAltGr | KeyModifierCapsLock
  );
}

QTEST_MAIN(ServerProxyTests)

#include "ServerProxyTests.moc"
