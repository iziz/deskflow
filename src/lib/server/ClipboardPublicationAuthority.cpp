/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClipboardPublicationAuthority.h"

#include "deskflow/ClipboardTransfer.h"

#include <utility>

namespace deskflow::server {

void ClipboardPublicationAuthority::recordFocus(std::string screen, uint32_t sequence)
{
  m_focusOwners.insert_or_assign(sequence, std::move(screen));
}

void ClipboardPublicationAuthority::removeScreen(std::string_view screen)
{
  for (auto focus = m_focusOwners.begin(); focus != m_focusOwners.end();) {
    if (focus->second == screen) {
      focus = m_focusOwners.erase(focus);
    } else {
      ++focus;
    }
  }
}

bool ClipboardPublicationAuthority::isFocusValid(std::string_view screen, uint32_t sequence) const
{
  const auto found = m_focusOwners.find(sequence);
  return found != m_focusOwners.end() && found->second == screen;
}

ClipboardPublicationAuthority::Decision ClipboardPublicationAuthority::evaluate(
    std::string_view screen, uint32_t sequence, std::string_view currentOwner, uint32_t currentSequence,
    std::string_view currentData, std::string_view data
) const
{
  if (!isFocusValid(screen, sequence)) {
    return Decision::InvalidFocus;
  }
  if (isClipboardSequenceOlder(sequence, currentSequence)) {
    return Decision::Superseded;
  }
  if (screen == currentOwner && sequence == currentSequence && data == currentData) {
    return Decision::Duplicate;
  }
  return Decision::Commit;
}

bool PendingClipboardPublication::begin(ClipboardID id, uint32_t sequence, uint32_t transferId)
{
  if (m_publication.has_value()) {
    return false;
  }
  m_publication = Publication{id, sequence, transferId};
  return true;
}

bool PendingClipboardPublication::active() const
{
  return m_publication.has_value();
}

bool PendingClipboardPublication::matches(ClipboardID id, uint32_t sequence) const
{
  return m_publication.has_value() && m_publication->id == id && m_publication->sequence == sequence;
}

void PendingClipboardPublication::cancel(uint32_t transferId)
{
  if (m_publication.has_value() && m_publication->transferId == transferId) {
    m_publication.reset();
  }
}

std::optional<uint32_t> PendingClipboardPublication::resolve(ClipboardID id, uint32_t sequence)
{
  if (!matches(id, sequence)) {
    return std::nullopt;
  }
  const auto transferId = m_publication->transferId;
  m_publication.reset();
  return transferId;
}

void PendingClipboardPublication::reset()
{
  m_publication.reset();
}

} // namespace deskflow::server
