/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardChunk.h"
#include "server/ClientProxy1_5.h"

#include <string>

class Server;
class IEventQueue;

//! Proxy for client implementing protocol version 1.6
class ClientProxy1_6 : public ClientProxy1_5
{
public:
  ClientProxy1_6(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_6() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard, uint32_t revision = 0) override;
  void supersedeClipboardTransfers(ClipboardID id) override;
  void beginClipboardSend() override;
  void finishClipboardSend() override;
  bool recvClipboard() override;

protected:
  void extendHeartbeatForClipboardTransfer(bool &extended);
  void restoreHeartbeatAfterClipboardTransfer(bool &extended);
  void extendHeartbeatForClipboardIncomingTransfer();
  void restoreHeartbeatAfterClipboardIncomingTransfer();
  void extendHeartbeatForClipboardOutgoingTransfer();
  void restoreHeartbeatAfterClipboardOutgoingTransfer();
  void handleInputProgress() override;

private:
  IEventQueue *m_events;
  ClipboardChunkAssembler m_legacyClipboardIncoming;
  uint64_t m_legacyClipboardGeneration[kClipboardEnd]{};
  bool m_legacyClipboardOutgoingActive = false;
  bool m_clipboardIncomingHeartbeatExtended = false;
  bool m_clipboardOutgoingHeartbeatExtended = false;
  double m_savedHeartbeatAlarm = 0.0;
};
