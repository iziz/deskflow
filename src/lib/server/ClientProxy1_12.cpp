/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_12.h"

ClientProxy1_12::ClientProxy1_12(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_11(name, adoptedStream, server, events)
{
}

bool ClientProxy1_12::usesAtomicClipboardPublish() const
{
  return true;
}
