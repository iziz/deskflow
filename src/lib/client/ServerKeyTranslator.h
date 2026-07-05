/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/KeyTypes.h"

#include <array>
#include <cstdint>

class ServerKeyTranslator
{
public:
  ServerKeyTranslator();

  void reset();
  KeyModifierID mapModifier(KeyModifierID id, uint32_t mappedId);

  KeyID translateKey(KeyID id) const;
  KeyModifierMask translateModifierMask(KeyModifierMask mask) const;

private:
  static KeyModifierID clampModifierID(uint32_t id);

  std::array<KeyModifierID, kKeyModifierIDLast> m_modifierTranslationTable{};
};
