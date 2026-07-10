/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "net/NetworkAddress.h"

#include <functional>
#include <string>

class EventQueueTimer;
class IDataSocket;
class IEventQueue;
class ISocketFactory;

namespace deskflow {
class IStream;
}

class ClipboardChannelClient
{
public:
  using ConnectedCallback = std::function<void(deskflow::IStream *)>;
  using FailedCallback = std::function<void()>;

  ClipboardChannelClient(
      IEventQueue *events, ISocketFactory *socketFactory, std::string clientName, ConnectedCallback connected,
      FailedCallback failed
  );
  ClipboardChannelClient(const ClipboardChannelClient &) = delete;
  ClipboardChannelClient(ClipboardChannelClient &&) = delete;
  ~ClipboardChannelClient();

  ClipboardChannelClient &operator=(const ClipboardChannelClient &) = delete;
  ClipboardChannelClient &operator=(ClipboardChannelClient &&) = delete;

  void connect(const NetworkAddress &address, bool secure, std::string token);
  void stop();

private:
  enum class Phase
  {
    Disconnected,
    Connecting,
    AwaitingHello,
    AwaitingReady
  };

  void bindNetworkInterface(IDataSocket *socket) const;
  void handleTransportConnected();
  void handleInput();
  void handleTimeout();
  void fail(const char *reason);
  void removeHandlers();
  void clearTimer();

  IEventQueue *m_events = nullptr;
  ISocketFactory *m_socketFactory = nullptr;
  std::string m_clientName;
  ConnectedCallback m_connected;
  FailedCallback m_failed;
  deskflow::IStream *m_stream = nullptr;
  EventQueueTimer *m_timer = nullptr;
  std::string m_token;
  Phase m_phase = Phase::Disconnected;
};
