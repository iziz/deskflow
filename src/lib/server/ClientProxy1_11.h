/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardChannelProtocol.h"
#include "server/ClientProxy1_10.h"

#include <string>

class ClientProxy1_11 : public ClientProxy1_10
{
public:
  ClientProxy1_11(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_11() override;

  void offerClipboardChannel() override;
  bool attachClipboardChannel(const std::string &token, deskflow::IStream *stream) override;
  bool parseMessage(const uint8_t *code) override;

protected:
  void onClipboardStreamDisconnected() override;

private:
  static std::string makeChannelToken();

  deskflow::ClipboardChannelToken m_channelToken;
  bool m_destroying = false;
};
