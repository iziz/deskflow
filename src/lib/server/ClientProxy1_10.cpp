/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_10.h"

ClientProxy1_10::ClientProxy1_10(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_10(name, adoptedStream, server, events, false)
{
}

ClientProxy1_10::ClientProxy1_10(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
    bool separateClipboardChannel
)
    : ClientProxy1_9(name, adoptedStream, server, events, true, separateClipboardChannel)
{
}
