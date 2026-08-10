/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */
#include "KeyMapTests.h"

#include "deskflow/KeyMap.h"

using namespace deskflow;
using KeyItemList = KeyMap::KeyItemList;
using KeyEntryList = std::vector<KeyItemList>;

namespace {
constexpr KeyButton kLetterButton = 10;
constexpr KeyButton kShiftButton = 20;
constexpr KeyButton kCapsLockButton = 21;

void addCapsLockLetterEntries(KeyMap &keyMap)
{
  KeyMap::KeyItem lowercase;
  lowercase.m_id = 't';
  lowercase.m_button = kLetterButton;
  lowercase.m_sensitive = KeyModifierShift;
  keyMap.addKeyEntry(lowercase);

  KeyMap::KeyItem uppercase = lowercase;
  uppercase.m_id = 'T';
  uppercase.m_required = KeyModifierShift;
  keyMap.addKeyEntry(uppercase);

  KeyMap::KeyItem shift;
  shift.m_id = kKeyShift_L;
  shift.m_button = kShiftButton;
  shift.m_generates = KeyModifierShift;
  keyMap.addKeyEntry(shift);

  KeyMap::KeyItem capsLock;
  capsLock.m_id = kKeyCapsLock;
  capsLock.m_button = kCapsLockButton;
  capsLock.m_generates = KeyModifierCapsLock;
  capsLock.m_lock = true;
  keyMap.addKeyEntry(capsLock);

  keyMap.finish();
}

bool containsButton(const KeyMap::Keystrokes &strokes, KeyButton button)
{
  for (const auto &stroke : strokes) {
    if (stroke.m_type == KeyMap::Keystroke::KeyType::Button && stroke.m_data.m_button.m_button == button) {
      return true;
    }
  }
  return false;
}
} // namespace

void KeyMapTests::findBestKey_requiredDown_matchExactFirstItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList;
  KeyMap::KeyItem item;
  item.m_required = KeyModifierShift;
  item.m_sensitive = KeyModifierShift;
  KeyModifierMask desiredState = KeyModifierShift;
  itemList.push_back(item);
  entryList.push_back(itemList);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 0);
}

void KeyMapTests::findBestKey_requiredAndExtraSensitiveDown_matchExactFirstItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList;
  KeyMap::KeyItem item;
  item.m_required = KeyModifierShift;
  item.m_sensitive = KeyModifierShift | KeyModifierAlt;
  KeyModifierMask desiredState = KeyModifierShift;
  itemList.push_back(item);
  entryList.push_back(itemList);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 0);
}

void KeyMapTests::findBestKey_requiredAndExtraSensitiveDown_matchExactSecondItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList1;
  KeyMap::KeyItem item1;
  item1.m_required = KeyModifierAlt;
  item1.m_sensitive = KeyModifierShift | KeyModifierAlt;
  KeyMap::KeyItemList itemList2;
  KeyMap::KeyItem item2;
  item2.m_required = KeyModifierShift;
  item2.m_sensitive = KeyModifierShift | KeyModifierAlt;
  KeyModifierMask desiredState = KeyModifierShift;
  itemList1.push_back(item1);
  itemList2.push_back(item2);
  entryList.push_back(itemList1);
  entryList.push_back(itemList2);
  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 1);
}

void KeyMapTests::findBestKey_extraSensitiveDown_matchExactSecondItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList1;
  KeyMap::KeyItem item1;
  item1.m_required = 0;
  item1.m_sensitive = KeyModifierAlt;
  KeyMap::KeyItemList itemList2;
  KeyMap::KeyItem item2;
  item2.m_required = 0;
  item2.m_sensitive = KeyModifierShift;
  KeyModifierMask desiredState = KeyModifierAlt;
  itemList1.push_back(item1);
  itemList2.push_back(item2);
  entryList.push_back(itemList1);
  entryList.push_back(itemList2);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 1);
}

void KeyMapTests::findBestKey_noRequiredDown_matchOneRequiredChangeItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList1;
  KeyMap::KeyItem item1;
  item1.m_required = KeyModifierShift | KeyModifierAlt;
  item1.m_sensitive = KeyModifierShift | KeyModifierAlt;
  KeyMap::KeyItemList itemList2;
  KeyMap::KeyItem item2;
  item2.m_required = KeyModifierShift;
  item2.m_sensitive = KeyModifierShift | KeyModifierAlt;
  KeyModifierMask desiredState = 0;
  itemList1.push_back(item1);
  itemList2.push_back(item2);
  entryList.push_back(itemList1);
  entryList.push_back(itemList2);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 1);
}

void KeyMapTests::findBestKey_onlyOneRequiredDown_matchTwoRequiredChangesItem()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList1;
  KeyMap::KeyItem item1;
  item1.m_required = KeyModifierShift | KeyModifierAlt | KeyModifierControl;
  item1.m_sensitive = KeyModifierShift | KeyModifierAlt | KeyModifierControl;
  KeyItemList itemList2;
  KeyMap::KeyItem item2;
  item2.m_required = KeyModifierShift | KeyModifierAlt;
  item2.m_sensitive = KeyModifierShift | KeyModifierAlt | KeyModifierControl;
  KeyModifierMask desiredState = 0;
  itemList1.push_back(item1);
  itemList2.push_back(item2);
  entryList.push_back(itemList1);
  entryList.push_back(itemList2);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), 1);
}

void KeyMapTests::findBestKey_noRequiredDown_cannotMatch()
{
  KeyMap keyMap;
  KeyEntryList entryList;
  KeyItemList itemList;
  KeyMap::KeyItem item;
  item.m_required = 0xffffffff;
  item.m_sensitive = 0xffffffff;
  KeyModifierMask desiredState = 0;
  itemList.push_back(item);
  entryList.push_back(itemList);

  QCOMPARE(keyMap.findBestKey(entryList, desiredState), -1);
}

void KeyMapTests::isCommand()
{
  KeyMap keyMap;
  KeyModifierMask mask = KeyModifierShift;
  QVERIFY(!keyMap.isCommand(mask));

  mask = KeyModifierControl;
  QVERIFY(keyMap.isCommand(mask));

  mask = KeyModifierAlt;
  QVERIFY(keyMap.isCommand(mask));

  mask = KeyModifierAltGr;
  QVERIFY(keyMap.isCommand(mask));

  mask = KeyModifierMeta;
  QVERIFY(keyMap.isCommand(mask));

  mask = KeyModifierSuper;
  QVERIFY(keyMap.isCommand(mask));
}

void KeyMapTests::mapkey()
{
  KeyMap keyMap{};
  KeyMap::Keystroke stroke('A', true, false, 1);
  KeyMap::KeyItem keyItem;
  keyItem.m_button = 'A';
  keyItem.m_group = 1;
  keyItem.m_id = 'A';
  keyMap.addKeyEntry(keyItem);
  keyMap.finish();
  KeyMap::Keystrokes strokes{stroke};
  KeyMap::ModifierToKeys activeModifiers{};
  KeyModifierMask currentState{};
  KeyModifierMask desiredMask{};
  auto result = keyMap.mapKey(strokes, kKeySetModifiers, 1, activeModifiers, currentState, desiredMask, false, "en");
  QVERIFY(result != nullptr);
  desiredMask = KeyModifierControl;
  result = keyMap.mapKey(strokes, kKeySetModifiers, 1, activeModifiers, currentState, desiredMask, false, "en");
  QVERIFY(result == nullptr);
}

void KeyMapTests::mapKey_capsLockUppercaseLetter_doesNotSynthesizeShift()
{
  KeyMap keyMap;
  addCapsLockLetterEntries(keyMap);
  KeyMap::Keystrokes strokes;
  KeyMap::ModifierToKeys activeModifiers;
  KeyModifierMask currentState = 0;

  const auto *result = keyMap.mapKey(strokes, 'T', 0, activeModifiers, currentState, KeyModifierCapsLock, false, "ko");

  QVERIFY(result != nullptr);
  QCOMPARE(result->m_id, static_cast<KeyID>('t'));
  QVERIFY(containsButton(strokes, kLetterButton));
  QVERIFY(!containsButton(strokes, kShiftButton));
}

void KeyMapTests::mapKey_unshiftedUppercaseLetter_doesNotSynthesizeShift()
{
  KeyMap keyMap;
  addCapsLockLetterEntries(keyMap);
  KeyMap::Keystrokes strokes;
  KeyMap::ModifierToKeys activeModifiers;
  KeyModifierMask currentState = 0;

  const auto *result = keyMap.mapKey(strokes, 'T', 0, activeModifiers, currentState, 0, false, "ko");

  QVERIFY(result != nullptr);
  QCOMPARE(result->m_id, static_cast<KeyID>('t'));
  QVERIFY(containsButton(strokes, kLetterButton));
  QVERIFY(!containsButton(strokes, kShiftButton));
}

void KeyMapTests::mapKey_capsLockAndShiftLowercaseLetter_preservesShift()
{
  KeyMap keyMap;
  addCapsLockLetterEntries(keyMap);
  KeyMap::Keystrokes strokes;
  KeyMap::ModifierToKeys activeModifiers;
  KeyModifierMask currentState = 0;

  const auto *result = keyMap.mapKey(
      strokes, 't', 0, activeModifiers, currentState, KeyModifierCapsLock | KeyModifierShift, false, "ko"
  );

  QVERIFY(result != nullptr);
  QCOMPARE(result->m_id, static_cast<KeyID>('T'));
  QVERIFY(containsButton(strokes, kLetterButton));
  QVERIFY(containsButton(strokes, kShiftButton));
}

void KeyMapTests::parseModifiers_plusKey_keepsPlusAsKey()
{
  std::string keystroke = "Control+Shift++";
  KeyModifierMask mask = 0;

  QVERIFY(KeyMap::parseModifiers(keystroke, mask));
  QCOMPARE(mask, static_cast<KeyModifierMask>(KeyModifierControl | KeyModifierShift));
  QCOMPARE(keystroke, std::string("+"));
}

void KeyMapTests::parseKey_plusSymbol_parsesAsAsciiKey()
{
  KeyID key = kKeyNone;

  QVERIFY(KeyMap::parseKey("+", key));
  QCOMPARE(key, static_cast<KeyID>('+'));
}

QTEST_MAIN(KeyMapTests)
