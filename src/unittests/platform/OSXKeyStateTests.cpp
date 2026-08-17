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
#include <cstddef>
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

  KeyModifierMask pollActiveModifiers() const override
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

  KeyModifierMask pollActiveModifiers() const override
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

QTEST_MAIN(OSXKeyStateTests)
