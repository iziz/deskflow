/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ServerKeyTranslator.h"

#include <algorithm>

ServerKeyTranslator::ServerKeyTranslator()
{
  reset();
}

void ServerKeyTranslator::reset()
{
  for (KeyModifierID id = 0; id < kKeyModifierIDLast; ++id) {
    m_modifierTranslationTable[id] = id;
  }
}

KeyModifierID ServerKeyTranslator::mapModifier(KeyModifierID id, uint32_t mappedId)
{
  if (id == kKeyModifierIDNull || id >= kKeyModifierIDLast) {
    return kKeyModifierIDNull;
  }

  m_modifierTranslationTable[id] = clampModifierID(mappedId);
  return m_modifierTranslationTable[id];
}

KeyID ServerKeyTranslator::translateKey(KeyID id) const
{
  static const KeyID s_translationTable[kKeyModifierIDLast][2] = {
      {kKeyNone, kKeyNone},     {kKeyShift_L, kKeyShift_R}, {kKeyControl_L, kKeyControl_R}, {kKeyAlt_L, kKeyAlt_R},
      {kKeyMeta_L, kKeyMeta_R}, {kKeySuper_L, kKeySuper_R}, {kKeyAltGr, kKeyAltGr}
  };

  KeyModifierID modifierId = kKeyModifierIDNull;
  uint32_t side = 0;
  switch (id) {
  case kKeyShift_L:
    modifierId = kKeyModifierIDShift;
    side = 0;
    break;

  case kKeyShift_R:
    modifierId = kKeyModifierIDShift;
    side = 1;
    break;

  case kKeyControl_L:
    modifierId = kKeyModifierIDControl;
    side = 0;
    break;

  case kKeyControl_R:
    modifierId = kKeyModifierIDControl;
    side = 1;
    break;

  case kKeyAlt_L:
    modifierId = kKeyModifierIDAlt;
    side = 0;
    break;

  case kKeyAlt_R:
    modifierId = kKeyModifierIDAlt;
    side = 1;
    break;

  case kKeyAltGr:
    modifierId = kKeyModifierIDAltGr;
    side = 1;
    break;

  case kKeyMeta_L:
    modifierId = kKeyModifierIDMeta;
    side = 0;
    break;

  case kKeyMeta_R:
    modifierId = kKeyModifierIDMeta;
    side = 1;
    break;

  case kKeySuper_L:
    modifierId = kKeyModifierIDSuper;
    side = 0;
    break;

  case kKeySuper_R:
    modifierId = kKeyModifierIDSuper;
    side = 1;
    break;

  default:
    break;
  }

  if (modifierId == kKeyModifierIDNull) {
    return id;
  }

  return s_translationTable[clampModifierID(m_modifierTranslationTable[modifierId])][side];
}

KeyModifierMask ServerKeyTranslator::translateModifierMask(KeyModifierMask mask) const
{
  static const KeyModifierMask s_masks[kKeyModifierIDLast] = {0x0000,          KeyModifierShift, KeyModifierControl,
                                                              KeyModifierAlt,  KeyModifierMeta,  KeyModifierSuper,
                                                              KeyModifierAltGr};

  KeyModifierMask newMask = mask & ~(KeyModifierShift | KeyModifierControl | KeyModifierAlt | KeyModifierMeta |
                                     KeyModifierSuper | KeyModifierAltGr);
  if ((mask & KeyModifierShift) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDShift])];
  }
  if ((mask & KeyModifierControl) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDControl])];
  }
  if ((mask & KeyModifierAlt) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDAlt])];
  }
  if ((mask & KeyModifierAltGr) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDAltGr])];
  }
  if ((mask & KeyModifierMeta) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDMeta])];
  }
  if ((mask & KeyModifierSuper) != 0) {
    newMask |= s_masks[clampModifierID(m_modifierTranslationTable[kKeyModifierIDSuper])];
  }
  return newMask;
}

KeyModifierID ServerKeyTranslator::clampModifierID(uint32_t id)
{
  return std::clamp<KeyModifierID>(
      static_cast<KeyModifierID>(id), kKeyModifierIDNull, kKeyModifierIDLast - 1
  );
}
