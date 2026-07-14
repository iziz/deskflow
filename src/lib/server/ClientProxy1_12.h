/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/ClientProxy1_11.h"

class ClientProxy1_12 : public ClientProxy1_11
{
public:
  ClientProxy1_12(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);

  bool usesAtomicClipboardPublish() const override;
};
