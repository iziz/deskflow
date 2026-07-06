/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"
#include "deskflow/IClipboard.h"

#include <array>
#include <cstdint>
#include <string>

class ClientClipboardState
{
public:
  enum class CacheState
  {
    Empty,
    AwaitingServerAck,
    ServerAcknowledged
  };

  struct SendDecision
  {
    bool shouldSend = false;
    bool force = false;
    std::string data;
  };

  void reset();

  bool ownsClipboard(ClipboardID id) const;
  IClipboard::Time clipboardTime(ClipboardID id) const;
  uint32_t revision(ClipboardID id) const;
  CacheState cacheState(ClipboardID id) const;

  bool isRemoteRevisionStale(ClipboardID id, uint32_t revision) const;
  bool shouldIgnoreGrabbedClipboard(ClipboardID id, const std::string &data) const;

  void markServerClipboardGrabbed(ClipboardID id);
  void markRemoteClipboardApplied(ClipboardID id, uint32_t revision, std::string data);
  void updateCachedClipboard(ClipboardID id, IClipboard::Time time, std::string data);
  void markGrabbedClipboardIgnored(ClipboardID id, IClipboard::Time time);
  void markLocalClipboardOwned(ClipboardID id);
  void forgetSentClipboard(ClipboardID id);
  void markLocalClipboardAcknowledged(ClipboardID id);

  SendDecision markLocalClipboardReadForSend(ClipboardID id, IClipboard::Time time, std::string data);

private:
  struct ClipboardEntry
  {
    bool owns = false;
    CacheState cacheState = CacheState::Empty;
    IClipboard::Time time = 0;
    std::string data;
    uint32_t revision = 0;
  };

  static bool isValidId(ClipboardID id);

  std::array<ClipboardEntry, kClipboardEnd> m_clipboards;
};
