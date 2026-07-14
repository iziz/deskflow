/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace deskflow::server {

class ClipboardPublicationAuthority
{
public:
  enum class Decision
  {
    Commit,
    Duplicate,
    InvalidFocus,
    Superseded
  };

  void recordFocus(std::string screen, uint32_t sequence);
  void removeScreen(std::string_view screen);
  bool isFocusValid(std::string_view screen, uint32_t sequence) const;
  Decision evaluate(
      std::string_view screen, uint32_t sequence, std::string_view currentOwner, uint32_t currentSequence,
      std::string_view currentData, std::string_view data
  ) const;

private:
  // Retain exact grants until the screen disconnects so in-flight transfers
  // remain valid across any number of later focus transitions.
  std::map<uint32_t, std::string> m_focusOwners;
};

class PendingClipboardPublication
{
public:
  bool begin(ClipboardID id, uint32_t sequence, uint32_t transferId);
  bool active() const;
  bool matches(ClipboardID id, uint32_t sequence) const;
  void cancel(uint32_t transferId);
  std::optional<uint32_t> resolve(ClipboardID id, uint32_t sequence);
  void reset();

private:
  struct Publication
  {
    ClipboardID id;
    uint32_t sequence;
    uint32_t transferId;
  };

  std::optional<Publication> m_publication;
};

} // namespace deskflow::server
