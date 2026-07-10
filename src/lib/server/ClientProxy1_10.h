/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_9.h"

class ClientProxy1_10 : public ClientProxy1_9
{
public:
  ClientProxy1_10(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);

protected:
  ClientProxy1_10(
      const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
      bool separateClipboardChannel
  );
};
