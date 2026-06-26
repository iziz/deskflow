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
#include <cstddef>
#include <vector>

#define SHIFT_ID_L kKeyShift_L
#define SHIFT_ID_R kKeyShift_R
#define SHIFT_BUTTON 57
#define A_CHAR_ID 0x00000061
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
  QCOMPARE(
      static_cast<uint32_t>(eventQueue.keyEvents[0].type), static_cast<uint32_t>(EventTypes::KeyStateKeyUp)
  );
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

void OSXKeyStateTests::fakePollShift()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(SHIFT_ID_L, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyDown(SHIFT_ID_R, 0, 2, "en");
  QVERIFY(isKeyPressed(keyState, SHIFT_BUTTON));

  keyState.fakeKeyUp(2);
  QVERIFY(!isKeyPressed(keyState, SHIFT_BUTTON));
}

void OSXKeyStateTests::fakePollChar()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, 0, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

void OSXKeyStateTests::fakePollCharWithModifier()
{
  deskflow::KeyMap keyMap;
  EventQueue eventQueue;
  OSXKeyState keyState(&eventQueue, keyMap, {"en"}, true);
  keyState.updateKeyMap();

  keyState.fakeKeyDown(A_CHAR_ID, KeyModifierShift, 1, "en");
  QVERIFY(isKeyPressed(keyState, A_CHAR_BUTTON));

  keyState.fakeKeyUp(1);
  QVERIFY(!isKeyPressed(keyState, A_CHAR_BUTTON));

  // HACK: delete the key in case it was typed into a text editor.
  // we should really set focus to an invisible window.
  keyState.fakeKeyDown(kKeyBackSpace, 0, 2, "en");
  keyState.fakeKeyUp(2);
}

bool OSXKeyStateTests::isKeyPressed(const OSXKeyState &keyState, KeyButton button)
{
  // HACK: allow os to realize key state changes.
  Arch::sleep(.2);

  IKeyState::KeyButtonSet pressed;
  keyState.pollPressedKeys(pressed);

  IKeyState::KeyButtonSet::const_iterator it;
  for (it = pressed.begin(); it != pressed.end(); ++it) {
    LOG_DEBUG("checking key %d", *it);
    if (*it == button) {
      return true;
    }
  }
  return false;
}

QTEST_MAIN(OSXKeyStateTests)
