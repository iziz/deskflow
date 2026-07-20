/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace deskflow::server {

class ClipboardPublicationAuthority
{
public:
  static constexpr size_t kMaximumRetainedFocusGrants = 4096;

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
  size_t retainedFocusCount() const;
  Decision evaluate(
      std::string_view screen, uint32_t sequence, std::string_view currentOwner, uint32_t currentSequence,
      std::string_view currentData, std::string_view data
  ) const;

private:
  // Retain enough issued epochs for in-flight transfers while enforcing a
  // deterministic memory bound on long-running servers.
  std::map<uint32_t, std::string> m_focusOwners;
  std::deque<uint32_t> m_focusOrder;
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
