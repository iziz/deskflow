/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClientClipboardState.h"

#include "deskflow/ClipboardTransfer.h"

void ClientClipboardState::reset()
{
  for (auto &clipboard : m_clipboards) {
    clipboard = ClipboardEntry{};
  }
}

bool ClientClipboardState::ownsClipboard(ClipboardID id) const
{
  return isValidId(id) && m_clipboards[id].owns;
}

IClipboard::Time ClientClipboardState::clipboardTime(ClipboardID id) const
{
  return isValidId(id) ? m_clipboards[id].time : 0;
}

uint32_t ClientClipboardState::revision(ClipboardID id) const
{
  return isValidId(id) ? m_clipboards[id].revision : 0;
}

ClientClipboardState::CacheState ClientClipboardState::cacheState(ClipboardID id) const
{
  return isValidId(id) ? m_clipboards[id].cacheState : CacheState::Empty;
}

bool ClientClipboardState::isRemoteRevisionStale(ClipboardID id, uint32_t revision) const
{
  return isValidId(id) && revision != 0 && isClipboardSequenceOlder(revision, m_clipboards[id].revision);
}

bool ClientClipboardState::shouldIgnoreGrabbedClipboard(ClipboardID id, const std::string &data) const
{
  if (!isValidId(id)) {
    return false;
  }

  const auto &clipboard = m_clipboards[id];
  return clipboard.cacheState != CacheState::Empty && data == clipboard.data;
}

void ClientClipboardState::markServerClipboardGrabbed(ClipboardID id)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  clipboard.owns = false;
  clipboard.cacheState = CacheState::Empty;
}

void ClientClipboardState::markRemoteClipboardApplied(ClipboardID id, uint32_t revision, std::string data)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  clipboard.revision = revision;
  clipboard.owns = false;
  clipboard.cacheState = CacheState::ServerAcknowledged;
  clipboard.data = std::move(data);
}

void ClientClipboardState::updateCachedClipboard(ClipboardID id, IClipboard::Time time, std::string data)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  clipboard.time = time;
  clipboard.data = std::move(data);
}

void ClientClipboardState::markGrabbedClipboardIgnored(ClipboardID id, IClipboard::Time time)
{
  if (!isValidId(id)) {
    return;
  }

  m_clipboards[id].time = time;
}

void ClientClipboardState::markLocalClipboardOwned(ClipboardID id)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  clipboard.owns = true;
  clipboard.cacheState = CacheState::Empty;
  clipboard.time = 0;
}

void ClientClipboardState::forgetSentClipboard(ClipboardID id)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  clipboard.cacheState = CacheState::Empty;
  clipboard.time = 0;
  clipboard.data.clear();
}

void ClientClipboardState::markLocalClipboardAcknowledged(ClipboardID id)
{
  if (!isValidId(id)) {
    return;
  }

  auto &clipboard = m_clipboards[id];
  if (clipboard.cacheState == CacheState::AwaitingServerAck) {
    clipboard.cacheState = CacheState::ServerAcknowledged;
  }
}

ClientClipboardState::SendDecision ClientClipboardState::markLocalClipboardReadForSend(
    ClipboardID id, IClipboard::Time time, std::string data
)
{
  if (!isValidId(id)) {
    return {};
  }

  auto &clipboard = m_clipboards[id];
  clipboard.time = time;

  const bool force = clipboard.cacheState == CacheState::Empty;
  if (!force && data == clipboard.data) {
    return {};
  }

  clipboard.cacheState = CacheState::AwaitingServerAck;
  clipboard.data = std::move(data);
  return {true, force};
}

bool ClientClipboardState::isValidId(ClipboardID id)
{
  return id < kClipboardEnd;
}
