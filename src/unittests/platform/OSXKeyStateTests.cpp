/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "OSXKeyStateTests.h"

#include "base/EventQueue.h"

#include <ApplicationServices/ApplicationServices.h>
#include <IOKit/hidsystem/IOLLEvent.h>
#include <array>
#include <cstddef>
#include <random>
#include <set>
#include <vector>

#define A_CHAR_BUTTON 001

namespace {

struct RecordedKeyEvent
{
  EventTypes type;
  KeyID key;
  KeyModifierMask mask;
  KeyButton button;
};

class RecordingEventQueue : public IEventQueue
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

  void addEvent(Event &&event) override
  {
    switch (event.getType()) {
      using enum EventTypes;
    case KeyStateKeyDown:
    case KeyStateKeyUp:
    case KeyStateKeyRepeat: {
      auto *info = static_cast<IKeyState::KeyInfo *>(event.getData());
      keyEvents.push_back({event.getType(), info->m_key, info->m_mask, info->m_button});
      break;
    }
    default:
      break;
    }
    Event::deleteData(event);
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
    return nullptr;
  }

  std::vector<RecordedKeyEvent> keyEvents;
};

class TestOSXKeyState : public OSXKeyState
{
public:
  using OSXKeyState::OSXKeyState;

  void setShadowModifiersForTest(KeyModifierMask mask)
  {
    setShadowModifiers(mask);
  }

  void setPolledModifiersForTest(KeyModifierMask mask)
  {
    m_polledModifiers = mask;
  }

  KeyModifierMask pollSystemModifiers() const override
  {
    return m_polledModifiers;
  }

private:
  KeyModifierMask m_polledModifiers{0};
};

struct NativeKeyPost
{
  CGKeyCode virtualKey;
  bool keyDown;
  CGEventFlags flags;
  bool fallback;
};

class NativePostOSXKeyState : public OSXKeyState
{
public:
  using OSXKeyState::OSXKeyState;

  void injectVirtualKey(CGKeyCode virtualKey, bool keyDown)
  {
    OSXKeyState::fakeKey(Keystroke(static_cast<KeyButton>(virtualKey + 1), keyDown, false, 0));
  }

  void setPolledModifiersForTest(KeyModifierMask mask)
  {
    m_polledModifiers = mask;
  }

  KeyModifierMask pollSystemModifiers() const override
  {
    return m_polledModifiers;
  }

  kern_return_t postHIDVirtualKey(uint8_t virtualKey, bool keyDown, CGEventFlags flags) override
  {
    posts.push_back({virtualKey, keyDown, flags, false});
    return hidResult;
  }

  bool postKeyboardKey(CGKeyCode virtualKey, bool keyDown, CGEventFlags flags) override
  {
    posts.push_back({virtualKey, keyDown, flags, true});
    return fallbackSucceeds;
  }

  kern_return_t hidResult{KERN_SUCCESS};
  bool fallbackSucceeds{true};
  std::vector<NativeKeyPost> posts;

private:
  KeyModifierMask m_polledModifiers{0};
};

constexpr KeyModifierMask kTrackedOSXShadowModifiers =
    KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierSuper | KeyModifierCapsLock;

struct OSXModifierSpec
{
  const char *name;
  CGKeyCode virtualKey;
  CGEventFlags aggregateFlag;
  CGEventFlags sideFlag;
};

constexpr std::array<OSXModifierSpec, 8> kOSXModifierSpecs{{
    {"left Shift", kVK_Shift, kCGEventFlagMaskShift, NX_DEVICELSHIFTKEYMASK},
    {"right Shift", kVK_RightShift, kCGEventFlagMaskShift, NX_DEVICERSHIFTKEYMASK},
    {"left Control", kVK_Control, kCGEventFlagMaskControl, NX_DEVICELCTLKEYMASK},
    {"right Control", kVK_RightControl, kCGEventFlagMaskControl, NX_DEVICERCTLKEYMASK},
    {"left Alt", kVK_Option, kCGEventFlagMaskAlternate, NX_DEVICELALTKEYMASK},
    {"right Alt", kVK_RightOption, kCGEventFlagMaskAlternate, NX_DEVICERALTKEYMASK},
    {"left Command", kVK_Command, kCGEventFlagMaskCommand, NX_DEVICELCMDKEYMASK},
    {"right Command", kVK_RightCommand, kCGEventFlagMaskCommand, NX_DEVICERCMDKEYMASK},
}};

constexpr CGEventFlags kTrackedOSXEventFlags =
    kCGEventFlagMaskShift | kCGEventFlagMaskControl | kCGEventFlagMaskAlternate | kCGEventFlagMaskCommand |
    NX_DEVICELSHIFTKEYMASK | NX_DEVICERSHIFTKEYMASK | NX_DEVICELCTLKEYMASK | NX_DEVICERCTLKEYMASK |
    NX_DEVICELALTKEYMASK | NX_DEVICERALTKEYMASK | NX_DEVICELCMDKEYMASK | NX_DEVICERCMDKEYMASK;

CGEventFlags expectedOSXModifierFlags(const std::set<CGKeyCode> &downModifiers)
{
  CGEventFlags flags = 0;
  for (const auto &modifier : kOSXModifierSpecs) {
    if (downModifiers.contains(modifier.virtualKey)) {
      flags |= modifier.aggregateFlag | modifier.sideFlag;
    }
  }
  return flags;
}

KeyModifierMask getShadowModifierMask(const OSXKeyState &keyState)
{
  return keyState.mapModifiersFromOSX(keyState.getModifierStateAsOSXFlags()) & kTrackedOSXShadowModifiers;
}

} // namespace

void OSXKeyStateTests::initTestCase()
{
  m_arch.init();
  m_log.setFilter(LogLevel::Level::Verbose);
}

void OSXKeyStateTests::mapModifiersFromOSX_OSXMask()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  KeyModifierMask outMask = 0;

  uint32_t shiftMask = 0 | kCGEventFlagMaskShift;
  outMask = keyState.mapModifiersFromOSX(shiftMask);
  QCOMPARE(outMask, KeyModifierShift);

  uint32_t ctrlMask = 0 | kCGEventFlagMaskControl;
  outMask = keyState.mapModifiersFromOSX(ctrlMask);
  QCOMPARE(outMask, KeyModifierControl);

  uint32_t altMask = 0 | kCGEventFlagMaskAlternate;
  outMask = keyState.mapModifiersFromOSX(altMask);
  QCOMPARE(outMask, KeyModifierAlt);

  uint32_t cmdMask = 0 | kCGEventFlagMaskCommand;
  outMask = keyState.mapModifiersFromOSX(cmdMask);
  QCOMPARE(outMask, KeyModifierSuper);

  uint32_t capsMask = 0 | kCGEventFlagMaskAlphaShift;
  outMask = keyState.mapModifiersFromOSX(capsMask);
  QCOMPARE(outMask, KeyModifierCapsLock);

  uint32_t numMask = 0 | kCGEventFlagMaskNumericPad;
  outMask = keyState.mapModifiersFromOSX(numMask);
  QCOMPARE(outMask, KeyModifierNumLock);
}

void OSXKeyStateTests::mapKeyFromEventUsesEventModifierFlags()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.onKey(A_CHAR_BUTTON, true, KeyModifierSuper);

  OSXKeyState::KeyIDs ids;
  KeyModifierMask mask = KeyModifierSuper;
  CGEventRef event = CGEventCreateKeyboardEvent(nullptr, kVK_ANSI_L, false);
  QVERIFY(event != nullptr);

  CGEventSetFlags(event, 0);
  QCOMPARE(keyState.mapKeyFromEvent(ids, &mask, event), static_cast<KeyButton>(kVK_ANSI_L + 1));
  QCOMPARE(mask, static_cast<KeyModifierMask>(0));

  CGEventSetFlags(event, kCGEventFlagMaskCommand);
  QCOMPARE(keyState.mapKeyFromEvent(ids, &mask, event), static_cast<KeyButton>(kVK_ANSI_L + 1));
  QCOMPARE(mask, KeyModifierSuper);

  CFRelease(event);
}

void OSXKeyStateTests::syncModifiersFromOSX_releasesStaleSuper()
{
  deskflow::KeyMap keyMap;
  RecordingEventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  auto *target = reinterpret_cast<void *>(0x1);

  keyState.handleModifierKeys(target, 0, KeyModifierSuper);
  eventQueue.keyEvents.clear();

  keyState.syncModifiersFromOSX(target, 0);

  QCOMPARE(keyState.getActiveModifiers(), 0);
  QCOMPARE(eventQueue.keyEvents.size(), std::size_t{1});
  QCOMPARE(static_cast<uint32_t>(eventQueue.keyEvents[0].type), static_cast<uint32_t>(EventTypes::KeyStateKeyUp));
  QCOMPARE(eventQueue.keyEvents[0].key, kKeySuper_L);
  QCOMPARE(eventQueue.keyEvents[0].mask, 0);
}

void OSXKeyStateTests::syncModifiersFromOSX_ignoresNumericPadFlag()
{
  deskflow::KeyMap keyMap;
  RecordingEventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  auto *target = reinterpret_cast<void *>(0x1);

  keyState.syncModifiersFromOSX(target, kCGEventFlagMaskNumericPad);

  QCOMPARE(keyState.getActiveModifiers(), 0);
  QVERIFY(eventQueue.keyEvents.empty());
}

void OSXKeyStateTests::syncToggleModifiers_ignoresRemoteNumLock()
{
  deskflow::KeyMap keyMap;
  deskflow::KeyMap::KeyItem numLock;
  numLock.m_id = kKeyNumLock;
  numLock.m_group = 0;
  numLock.m_button = static_cast<KeyButton>(kVK_ANSI_KeypadClear + 1);
  deskflow::KeyMap::initModifierKey(numLock);
  keyMap.addKeyEntry(numLock);
  keyMap.addHalfDuplexButton(numLock.m_button);
  keyMap.finish();

  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  QVERIFY(keyState.syncToggleModifiers(KeyModifierNumLock));

  QCOMPARE(keyState.getActiveModifiers(), static_cast<KeyModifierMask>(0));
  QVERIFY(keyState.posts.empty());
}

void OSXKeyStateTests::pollActiveModifiers_clientIgnoresAggregateNonToggleModifiers()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true, false);
  keyState.setPolledModifiersForTest(KeyModifierShift | KeyModifierControl | KeyModifierCapsLock);

  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(KeyModifierCapsLock));
}

void OSXKeyStateTests::syncToggleModifiers_clientReleasesModifierAbsentFromSourceKeyboard()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true, false);
  keyState.setPolledModifiersForTest(KeyModifierShift);

  QVERIFY(keyState.syncToggleModifiers(0));

  QCOMPARE(keyState.posts.size(), std::size_t{2});
  QCOMPARE(keyState.posts[0].virtualKey, static_cast<CGKeyCode>(kVK_Shift));
  QVERIFY(!keyState.posts[0].keyDown);
  QCOMPARE(keyState.posts[0].flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
  QCOMPARE(keyState.posts[1].virtualKey, static_cast<CGKeyCode>(kVK_RightShift));
  QVERIFY(!keyState.posts[1].keyDown);
  QCOMPARE(keyState.posts[1].flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(0));
}

void OSXKeyStateTests::syncModifiersFromOSX_clearsStaleShadowWhenMaskUnchanged()
{
  deskflow::KeyMap keyMap;
  RecordingEventQueue eventQueue;
  TestOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  auto *target = reinterpret_cast<void *>(0x1);

  keyState.setShadowModifiersForTest(KeyModifierCapsLock | KeyModifierSuper);
  QCOMPARE(getShadowModifierMask(keyState), static_cast<KeyModifierMask>(KeyModifierCapsLock | KeyModifierSuper));

  keyState.syncModifiersFromOSX(target, 0);

  QCOMPARE(keyState.getActiveModifiers(), 0);
  QCOMPARE(getShadowModifierMask(keyState), static_cast<KeyModifierMask>(0));
  QVERIFY(eventQueue.keyEvents.empty());
}

void OSXKeyStateTests::clearStaleModifiers_refreshesShadowFromSystemState()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  TestOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  const auto systemMask = static_cast<KeyModifierMask>(KeyModifierCapsLock | KeyModifierSuper);
  keyState.setPolledModifiersForTest(systemMask);
  keyState.setShadowModifiersForTest(0);

  keyState.clearStaleModifiers();

  QCOMPARE(getShadowModifierMask(keyState), systemMask);
}

void OSXKeyStateTests::clearStaleModifiers_releasesSyntheticModifiersMissingFromSystemState()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.setPolledModifiersForTest(KeyModifierSuper);
  keyState.injectVirtualKey(kVK_Shift, true);
  keyState.injectVirtualKey(kVK_CapsLock, true);
  keyState.posts.clear();

  keyState.clearStaleModifiers();

  QCOMPARE(getShadowModifierMask(keyState), static_cast<KeyModifierMask>(KeyModifierSuper));
  QCOMPARE(keyState.posts.size(), std::size_t{2});
  QCOMPARE(keyState.posts[0].virtualKey, static_cast<CGKeyCode>(kVK_Shift));
  QVERIFY(!keyState.posts[0].keyDown);
  QCOMPARE(keyState.posts[1].virtualKey, static_cast<CGKeyCode>(kVK_CapsLock));
  QVERIFY(!keyState.posts[1].keyDown);
}

void OSXKeyStateTests::clearStaleModifiers_releasesPreviouslyPostedModifierAfterMatchingUp()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(kVK_Shift, true);
  keyState.injectVirtualKey(kVK_Shift, false);
  QCOMPARE(keyState.posts.size(), std::size_t{2});

  keyState.clearStaleModifiers();

  QCOMPARE(keyState.posts.size(), std::size_t{3});
  QCOMPARE(keyState.posts.back().virtualKey, static_cast<CGKeyCode>(kVK_Shift));
  QVERIFY(!keyState.posts.back().keyDown);
  QCOMPARE(keyState.posts.back().flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
}

void OSXKeyStateTests::clearStaleModifiers_doesNotAdoptPendingSyntheticModifierAsPhysical()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(kVK_Shift, true);
  keyState.injectVirtualKey(kVK_Shift, false);
  keyState.setPolledModifiersForTest(KeyModifierShift);
  keyState.posts.clear();

  keyState.clearStaleModifiers();

  QCOMPARE(keyState.posts.size(), std::size_t{1});
  QCOMPARE(keyState.posts.back().virtualKey, static_cast<CGKeyCode>(kVK_Shift));
  QVERIFY(!keyState.posts.back().keyDown);
  QCOMPARE(keyState.posts.back().flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
  QCOMPARE(getShadowModifierMask(keyState), static_cast<KeyModifierMask>(0));
  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(0));

  keyState.setPolledModifiersForTest(0);
  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(0));
  keyState.setPolledModifiersForTest(KeyModifierShift);
  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(KeyModifierShift));
}

void OSXKeyStateTests::clearStaleModifiers_doesNotFilterPhysicalCapsLockAsPendingRelease()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(kVK_CapsLock, true);
  keyState.setPolledModifiersForTest(KeyModifierCapsLock);

  keyState.clearStaleModifiers();

  QCOMPARE(keyState.pollActiveModifiers(), static_cast<KeyModifierMask>(KeyModifierCapsLock));
}

void OSXKeyStateTests::nativeKeyTransaction_appliesAuthoritativeFlagsToEveryKey()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(kVK_Shift, true);
  keyState.injectVirtualKey(kVK_ANSI_A, true);
  QVERIFY((keyState.posts.back().flags & kCGEventFlagMaskShift) != 0);

  keyState.injectVirtualKey(kVK_Shift, false);
  keyState.injectVirtualKey(kVK_ANSI_A, false);

  QCOMPARE(keyState.posts.back().virtualKey, static_cast<CGKeyCode>(kVK_ANSI_A));
  QCOMPARE(keyState.posts.back().flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
}

void OSXKeyStateTests::nativeKeyTransaction_preservesRightModifierSide()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(kVK_RightShift, true);

  QCOMPARE(keyState.posts.size(), std::size_t{1});
  QCOMPARE(keyState.posts[0].virtualKey, static_cast<CGKeyCode>(kVK_RightShift));
  QVERIFY((keyState.posts[0].flags & kCGEventFlagMaskShift) != 0);
  QVERIFY((keyState.posts[0].flags & NX_DEVICERSHIFTKEYMASK) != 0);
  QCOMPARE(keyState.posts[0].flags & NX_DEVICELSHIFTKEYMASK, static_cast<CGEventFlags>(0));
}

void OSXKeyStateTests::nativeKeyTransaction_rollsBackModifierAfterPostFailure()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.hidResult = KERN_FAILURE;
  keyState.fallbackSucceeds = false;

  keyState.injectVirtualKey(kVK_Shift, true);

  QCOMPARE(keyState.posts.size(), std::size_t{2});
  QVERIFY(keyState.posts[1].fallback);
  QCOMPARE(getShadowModifierMask(keyState), static_cast<KeyModifierMask>(0));
}

void OSXKeyStateTests::nativeKeyTransaction_repairsOrphanedClientModifierBeforeNextKey()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true, false);

  keyState.injectVirtualKey(kVK_Shift, true);
  keyState.setPolledModifiersForTest(KeyModifierShift);
  keyState.injectVirtualKey(kVK_Shift, false);

  // Simulate macOS still reporting Shift after accepting the queued key-up.
  keyState.posts.clear();
  keyState.injectVirtualKey(kVK_ANSI_A, true);

  QCOMPARE(keyState.posts.size(), std::size_t{3});
  QCOMPARE(keyState.posts[0].virtualKey, static_cast<CGKeyCode>(kVK_Shift));
  QVERIFY(!keyState.posts[0].keyDown);
  QCOMPARE(keyState.posts[1].virtualKey, static_cast<CGKeyCode>(kVK_RightShift));
  QVERIFY(!keyState.posts[1].keyDown);
  QCOMPARE(keyState.posts[2].virtualKey, static_cast<CGKeyCode>(kVK_ANSI_A));
  QVERIFY(keyState.posts[2].keyDown);
  QCOMPARE(keyState.posts[2].flags & kCGEventFlagMaskShift, static_cast<CGEventFlags>(0));
}

void OSXKeyStateTests::nativeKeyTransaction_modifierLifecycle_data()
{
  QTest::addColumn<int>("modifierIndex");
  for (std::size_t i = 0; i < kOSXModifierSpecs.size(); ++i) {
    QTest::newRow(kOSXModifierSpecs[i].name) << static_cast<int>(i);
  }
}

void OSXKeyStateTests::nativeKeyTransaction_modifierLifecycle()
{
  QFETCH(int, modifierIndex);
  const auto &modifier = kOSXModifierSpecs.at(static_cast<std::size_t>(modifierIndex));
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);

  keyState.injectVirtualKey(modifier.virtualKey, true);
  QCOMPARE(
      quint64(keyState.posts.back().flags & kTrackedOSXEventFlags),
      quint64(modifier.aggregateFlag | modifier.sideFlag)
  );

  keyState.injectVirtualKey(kVK_ANSI_A, true);
  QCOMPARE(
      quint64(keyState.posts.back().flags & kTrackedOSXEventFlags),
      quint64(modifier.aggregateFlag | modifier.sideFlag)
  );

  keyState.injectVirtualKey(modifier.virtualKey, false);
  QCOMPARE(quint64(keyState.posts.back().flags & kTrackedOSXEventFlags), quint64(0));

  keyState.injectVirtualKey(kVK_ANSI_F, true);
  QCOMPARE(quint64(keyState.posts.back().flags & kTrackedOSXEventFlags), quint64(0));

  keyState.injectVirtualKey(kVK_Escape, true);
  QCOMPARE(quint64(keyState.posts.back().flags & kTrackedOSXEventFlags), quint64(0));
}

void OSXKeyStateTests::nativeKeyTransaction_deterministicStateMachine()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  NativePostOSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  std::mt19937 random(0x5eedc0deu);
  std::set<CGKeyCode> downModifiers;
  m_log.setFilter(LogLevel::Level::Error);

  for (int step = 0; step < 4096; ++step) {
    const auto &modifier = kOSXModifierSpecs[random() % kOSXModifierSpecs.size()];
    const bool down = (random() & 1u) != 0u;
    if (down) {
      downModifiers.insert(modifier.virtualKey);
    } else {
      downModifiers.erase(modifier.virtualKey);
    }

    keyState.injectVirtualKey(modifier.virtualKey, down);
    const auto expected = expectedOSXModifierFlags(downModifiers);
    QVERIFY2(
        quint64(keyState.posts.back().flags & kTrackedOSXEventFlags) == quint64(expected),
        qPrintable(QString("modifier state mismatch at step %1").arg(step))
    );

    if ((step % 17) == 0) {
      keyState.injectVirtualKey(kVK_ANSI_A, true);
      QVERIFY2(
          quint64(keyState.posts.back().flags & kTrackedOSXEventFlags) == quint64(expected),
          qPrintable(QString("ordinary key inherited stale modifiers at step %1").arg(step))
      );
    }
  }

  for (const auto &modifier : kOSXModifierSpecs) {
    keyState.injectVirtualKey(modifier.virtualKey, false);
  }
  keyState.injectVirtualKey(kVK_ANSI_A, true);
  QCOMPARE(quint64(keyState.posts.back().flags & kTrackedOSXEventFlags), quint64(0));
  m_log.setFilter(LogLevel::Level::Verbose);
}

QTEST_MAIN(OSXKeyStateTests)
