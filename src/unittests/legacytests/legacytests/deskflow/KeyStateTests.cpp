/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2011 Nick Bolton
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "unittests/legacytests/mock/deskflow/MockEventQueue.h"
#include "unittests/legacytests/mock/deskflow/MockKeyMap.h"
#include "unittests/legacytests/mock/deskflow/MockKeyState.h"

#include <gmock/gmock.h>
#include <gtest/gtest.h>

using ::testing::_;
using ::testing::Invoke;
using ::testing::NiceMock;
using ::testing::Return;

void stubPollPressedKeys(IKeyState::KeyButtonSet &pressedKeys);

void assertMaskIsOne(ForeachKeyCallback, void *userData);

const deskflow::KeyMap::KeyItem *stubMapKey(
    deskflow::KeyMap::Keystrokes &keys, KeyID id, int32_t group, deskflow::KeyMap::ModifierToKeys &activeModifiers,
    KeyModifierMask &currentState, KeyModifierMask desiredMask, bool isAutoRepeat, const std::string &lang
);

const deskflow::KeyMap::KeyItem *stubSyncToggleModifiers(
    deskflow::KeyMap::Keystrokes &keys, KeyID id, int32_t group, deskflow::KeyMap::ModifierToKeys &activeModifiers,
    KeyModifierMask &currentState, KeyModifierMask desiredMask, bool isAutoRepeat, const std::string &lang
);

deskflow::KeyMap::Keystroke s_stubKeystroke(1, false, false);
deskflow::KeyMap::KeyItem s_stubKeyItem;

TEST(KeyStateTests, sendKeyEvent_halfDuplexAndRepeat_addEventNotCalled)
{
  NiceMock<MockKeyMap> keyMap;
  NiceMock<MockEventQueue> eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  ON_CALL(keyMap, isHalfDuplex(_, _)).WillByDefault(Return(true));

  EXPECT_CALL(eventQueue, addEvent(_)).Times(0);

  keyState.sendKeyEvent(nullptr, false, true, kKeyCapsLock, 0, 0, 0);
}

TEST(KeyStateTests, updateKeyMap_mockKeyMap_keyMapGotMock)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  // key map member gets a new key map via swap()
  EXPECT_CALL(keyMap, swap(_));

  keyState.updateKeyMap();
}

TEST(KeyStateTests, updateKeyState_activeModifiers_maskSet)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  ON_CALL(keyState, pollActiveModifiers()).WillByDefault(Return(KeyModifierAlt));

  keyState.updateKeyState();

  KeyModifierMask actual = keyState.getActiveModifiers();
  ASSERT_EQ(KeyModifierAlt, actual);
}

TEST(KeyStateTests, updateKeyState_activeModifiers_keyMapGotModifers)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  ON_CALL(keyState, pollActiveModifiers()).WillByDefault(Return(1));
  ON_CALL(keyMap, foreachKey(_, _)).WillByDefault(Invoke(assertMaskIsOne));

  // key map gets new modifiers via foreachKey()
  EXPECT_CALL(keyMap, foreachKey(_, _));

  keyState.updateKeyState();
}

TEST(KeyStateTests, setHalfDuplexMask_capsLock_halfDuplexCapsLockAdded)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  EXPECT_CALL(keyMap, addHalfDuplexModifier(kKeyCapsLock));

  keyState.setHalfDuplexMask(KeyModifierCapsLock);
}

TEST(KeyStateTests, setHalfDuplexMask_numLock_halfDuplexNumLockAdded)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  EXPECT_CALL(keyMap, addHalfDuplexModifier(kKeyNumLock));

  keyState.setHalfDuplexMask(KeyModifierNumLock);
}

TEST(KeyStateTests, setHalfDuplexMask_scrollLock_halfDuplexScollLockAdded)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  EXPECT_CALL(keyMap, addHalfDuplexModifier(kKeyScrollLock));

  keyState.setHalfDuplexMask(KeyModifierScrollLock);
}

TEST(KeyStateTests, syncToggleModifiers_capsLockOff_turnsCapsLockOn)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubSyncToggleModifiers));

  EXPECT_CALL(keyMap, mapKey(_, kKeySetModifiers, _, _, _, KeyModifierCapsLock, false, _));
  EXPECT_CALL(keyState, fakeKey(_)).Times(1);

  ASSERT_TRUE(keyState.syncToggleModifiers(KeyModifierCapsLock));
  ASSERT_EQ(KeyModifierCapsLock, keyState.getActiveModifiers());
}

TEST(KeyStateTests, syncToggleModifiers_capsLockOn_turnsCapsLockOff)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  keyState.onKey(0, false, KeyModifierCapsLock);
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubSyncToggleModifiers));

  EXPECT_CALL(keyMap, mapKey(_, kKeyClearModifiers, _, _, _, KeyModifierCapsLock, false, _));
  EXPECT_CALL(keyState, fakeKey(_)).Times(1);

  ASSERT_TRUE(keyState.syncToggleModifiers(0));
  ASSERT_EQ(0, keyState.getActiveModifiers());
}

TEST(KeyStateTests, syncToggleModifiers_nonToggleMask_doesNothing)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  EXPECT_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).Times(0);
  EXPECT_CALL(keyState, fakeKey(_)).Times(0);

  ASSERT_TRUE(keyState.syncToggleModifiers(KeyModifierControl | KeyModifierAlt));
  ASSERT_EQ(0, keyState.getActiveModifiers());
}

TEST(KeyStateTests, syncToggleModifiers_capsLock_roundTripsWithKeyMap)
{
  deskflow::KeyMap keyMap;
  deskflow::KeyMap::KeyItem capsLock;
  capsLock.m_id = kKeyCapsLock;
  capsLock.m_group = 0;
  capsLock.m_button = 1;
  deskflow::KeyMap::initModifierKey(capsLock);
  keyMap.addKeyEntry(capsLock);
  keyMap.finish();

  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  ON_CALL(keyState, pollActiveGroup()).WillByDefault(Return(0));
  EXPECT_CALL(keyState, fakeKey(_)).Times(4);

  ASSERT_TRUE(keyState.syncToggleModifiers(KeyModifierCapsLock));
  ASSERT_EQ(KeyModifierCapsLock, keyState.getActiveModifiers());
  ASSERT_TRUE(keyState.syncToggleModifiers(0));
  ASSERT_EQ(0, keyState.getActiveModifiers());
}

TEST(KeyStateTests, fakeKeyDown_serverKeyAlreadyDown_fakeKeyCalledTwice)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  s_stubKeyItem.m_client = 0;
  s_stubKeyItem.m_button = 1;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));

  // 2 calls to fakeKeyDown should still call fakeKey, even though
  // repeated keys are handled differently.
  EXPECT_CALL(keyState, fakeKey(_)).Times(2);

  // call twice to simulate server key already down (a misreported autorepeat).
  keyState.fakeKeyDown(1, 0, 0, "en");
  keyState.fakeKeyDown(1, 0, 0, "en");
}

TEST(KeyStateTests, fakeKeyDown_isIgnoredKey_fakeKeyNotCalled)
{
  MockKeyMap keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  EXPECT_CALL(keyState, fakeKey(_)).Times(0);

  keyState.fakeKeyDown(kKeyCapsLock, 0, 0, "en");
}

TEST(KeyStateTests, fakeKeyDown_mapReturnsKeystrokes_fakeKeyCalled)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  s_stubKeyItem.m_button = 0;
  s_stubKeyItem.m_client = 0;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));

  EXPECT_CALL(keyState, fakeKey(_)).Times(1);

  keyState.fakeKeyDown(1, 0, 0, "en");
}

TEST(KeyStateTests, fakeKeyRepeat_nullKey_returnsFalse)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  // set the key to down (we need to make mapKey return a valid key to do this).
  deskflow::KeyMap::KeyItem keyItem;
  keyItem.m_client = 0;
  keyItem.m_button = 1;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Return(&keyItem));
  keyState.fakeKeyDown(1, 0, 0, "en");

  // change mapKey to return nullptr so that fakeKeyRepeat exits early.
  deskflow::KeyMap::KeyItem *nullKeyItem = nullptr;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Return(nullKeyItem));

  bool actual = keyState.fakeKeyRepeat(1, 0, 0, 0, "en");

  ASSERT_FALSE(actual);
}

TEST(KeyStateTests, fakeKeyRepeat_invalidButton_returnsFalse)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  // set the key to down (we need to make mapKey return a valid key to do this).
  deskflow::KeyMap::KeyItem keyItem;
  keyItem.m_client = 0;
  keyItem.m_button = 1; // set to 1 to make fakeKeyDown work.
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Return(&keyItem));
  keyState.fakeKeyDown(1, 0, 0, "en");

  // change button to 0 so that fakeKeyRepeat will return early.
  keyItem.m_button = 0;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Return(&keyItem));

  bool actual = keyState.fakeKeyRepeat(1, 0, 0, 0, "en");

  ASSERT_FALSE(actual);
}

TEST(KeyStateTests, fakeKeyRepeat_validKey_returnsTrue)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);
  s_stubKeyItem.m_client = 0;
  s_stubKeystroke.m_type = deskflow::KeyMap::Keystroke::KeyType::Button;
  s_stubKeystroke.m_data.m_button.m_button = 2;

  // set the button to 1 for fakeKeyDown call
  s_stubKeyItem.m_button = 1;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));
  keyState.fakeKeyDown(1, 0, 0, "en");

  // change the button to 2
  s_stubKeyItem.m_button = 2;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));

  bool actual = keyState.fakeKeyRepeat(1, 0, 0, 0, "en");

  ASSERT_TRUE(actual);
}

TEST(KeyStateTests, fakeKeyUp_buttonAlreadyDown_returnsTrue)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  // press alt down so we get full coverage.
  ON_CALL(keyState, pollActiveModifiers()).WillByDefault(Return(KeyModifierAlt));
  keyState.updateKeyState();

  // press button 1 down.
  s_stubKeyItem.m_button = 1;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));
  keyState.fakeKeyDown(1, 0, 1, "en");

  // this takes the button id, which is the 3rd arg of fakeKeyDown
  bool actual = keyState.fakeKeyUp(1);

  ASSERT_TRUE(actual);
}

TEST(KeyStateTests, fakeAllKeysUp_keysWereDown_keysAreUp)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  // press button 1 down.
  s_stubKeyItem.m_button = 1;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));
  keyState.fakeKeyDown(1, 0, 1, "en");

  // method under test
  keyState.fakeAllKeysUp();

  bool actual = keyState.isKeyDown(1);
  ASSERT_FALSE(actual);
}

TEST(KeyStateTests, updateKeyState_syntheticKeyDown_releasesBeforePolling)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  s_stubKeyItem.m_button = 1;
  s_stubKeyItem.m_client = 0;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));

  EXPECT_CALL(keyState, fakeKey(_)).Times(2);
  keyState.fakeKeyDown(1, 0, 1, "en");

  keyState.updateKeyState();

  ASSERT_FALSE(keyState.isKeyDown(1));
  ASSERT_FALSE(keyState.fakeKeyUp(1));
}

TEST(KeyStateTests, fakeAllKeysUp_physicalModifierAndRegularKey_doesNotSynthesizeReleases)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  keyState.onKey(0x1d, true, KeyModifierControl);
  keyState.onKey(0x2e, true, KeyModifierControl);

  EXPECT_CALL(keyState, fakeKey(_)).Times(0);

  keyState.fakeAllKeysUp();
}

TEST(KeyStateTests, updateKeyState_physicalLeftAndRightModifiers_doesNotSynthesizeReleases)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  keyState.onKey(0x02a, true, KeyModifierShift);
  keyState.onKey(0x036, true, KeyModifierShift);
  keyState.onKey(0x01d, true, KeyModifierShift | KeyModifierControl);
  keyState.onKey(0x11d, true, KeyModifierShift | KeyModifierControl);
  keyState.onKey(0x038, true, KeyModifierShift | KeyModifierControl | KeyModifierAlt);
  keyState.onKey(0x138, true, KeyModifierShift | KeyModifierControl | KeyModifierAlt);
  keyState.onKey(0x15b, true, KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierSuper);
  keyState.onKey(0x15c, true, KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierSuper);

  EXPECT_CALL(keyState, fakeKey(_)).Times(0);

  keyState.updateKeyState();
}

TEST(KeyStateTests, updateKeyState_mixedPhysicalChordAndSyntheticKey_releasesOnlySyntheticKey)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  s_stubKeystroke.m_type = deskflow::KeyMap::Keystroke::KeyType::Button;
  s_stubKeystroke.m_data.m_button.m_button = 1;
  s_stubKeyItem.m_button = 1;
  s_stubKeyItem.m_client = 0;
  ON_CALL(keyMap, mapKey(_, _, _, _, _, _, _, _)).WillByDefault(Invoke(stubMapKey));
  ON_CALL(keyState, pollPressedKeys(_)).WillByDefault(Invoke([](IKeyState::KeyButtonSet &pressedKeys) {
    pressedKeys.insert(0x1d);
    pressedKeys.insert(0x2e);
  }));

  EXPECT_CALL(keyState, fakeKey(_)).Times(2);

  keyState.fakeKeyDown(1, 0, 1, "en");
  keyState.onKey(0x1d, true, KeyModifierControl);
  keyState.onKey(0x2e, true, KeyModifierControl);
  keyState.updateKeyState();

  ASSERT_FALSE(keyState.isKeyDown(1));
  ASSERT_TRUE(keyState.isKeyDown(0x1d));
  ASSERT_TRUE(keyState.isKeyDown(0x2e));

  keyState.onKey(0x1d, false, 0);
  keyState.onKey(0x2e, false, 0);

  ASSERT_FALSE(keyState.isKeyDown(0x1d));
  ASSERT_FALSE(keyState.isKeyDown(0x2e));
}

TEST(KeyStateTests, updateKeyState_physicalControlWithToggleModifiers_preservesPolledToggleState)
{
  NiceMock<MockKeyMap> keyMap;
  MockEventQueue eventQueue;
  KeyStateImpl keyState(eventQueue, keyMap);

  constexpr auto toggleModifiers = KeyModifierCapsLock | KeyModifierNumLock | KeyModifierScrollLock;
  ON_CALL(keyState, pollActiveModifiers()).WillByDefault(Return(toggleModifiers));
  keyState.onKey(0x1d, true, toggleModifiers | KeyModifierControl);

  EXPECT_CALL(keyState, fakeKey(_)).Times(0);

  keyState.updateKeyState();

  ASSERT_EQ(toggleModifiers, keyState.getActiveModifiers());
}

void stubPollPressedKeys(IKeyState::KeyButtonSet &pressedKeys)
{
  pressedKeys.insert(1);
}

void assertMaskIsOne(ForeachKeyCallback, void *userData)
{
  ASSERT_EQ(1, ((KeyState::AddActiveModifierContext *)userData)->m_mask);
}

const deskflow::KeyMap::KeyItem *stubMapKey(
    deskflow::KeyMap::Keystrokes &keys, KeyID, int32_t, deskflow::KeyMap::ModifierToKeys &, KeyModifierMask &,
    KeyModifierMask, bool, const std::string &
)
{
  keys.push_back(s_stubKeystroke);
  return &s_stubKeyItem;
}

const deskflow::KeyMap::KeyItem *stubSyncToggleModifiers(
    deskflow::KeyMap::Keystrokes &keys, KeyID id, int32_t, deskflow::KeyMap::ModifierToKeys &,
    KeyModifierMask &currentState, KeyModifierMask desiredMask, bool, const std::string &
)
{
  if (id == kKeySetModifiers) {
    currentState |= desiredMask;
  } else if (id == kKeyClearModifiers) {
    currentState &= ~desiredMask;
  } else {
    return nullptr;
  }

  keys.push_back(s_stubKeystroke);
  return &s_stubKeyItem;
}
