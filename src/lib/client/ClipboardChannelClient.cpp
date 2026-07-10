/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClipboardChannelClient.h"

#include "arch/Arch.h"
#include "base/BaseException.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "common/NetworkProtocol.h"
#include "common/Settings.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "net/IDataSocket.h"
#include "net/ISocketFactory.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <utility>

namespace {
constexpr double kClipboardChannelConnectTimeout = 10.0;
}

ClipboardChannelClient::ClipboardChannelClient(
    IEventQueue *events, ISocketFactory *socketFactory, std::string clientName, ConnectedCallback connected,
    FailedCallback failed
)
    : m_events(events),
      m_socketFactory(socketFactory),
      m_clientName(std::move(clientName)),
      m_connected(std::move(connected)),
      m_failed(std::move(failed))
{
}

ClipboardChannelClient::~ClipboardChannelClient()
{
  stop();
}

void ClipboardChannelClient::connect(const NetworkAddress &address, bool secure, std::string token)
{
  stop();
  m_token = std::move(token);
  m_phase = Phase::Connecting;

  try {
    const auto securityLevel = secure ? SecurityLevel::PeerAuth : SecurityLevel::PlainText;
    auto socket =
        std::unique_ptr<IDataSocket>(m_socketFactory->create(ARCH->getAddrFamily(address.getAddress()), securityLevel));
    auto *rawSocket = socket.get();
    bindNetworkInterface(rawSocket);
    m_stream = new PacketStreamFilter(m_events, rawSocket, true);
    socket.release();

    const auto target = m_stream->getEventTarget();
    const auto connectedType = secure ? EventTypes::DataSocketSecureConnected : EventTypes::DataSocketConnected;
    m_events->addHandler(connectedType, target, [this](const auto &) { handleTransportConnected(); });
    m_events->addHandler(EventTypes::DataSocketConnectionFailed, target, [this](const auto &event) {
      auto *info = static_cast<IDataSocket::ConnectionFailedInfo *>(event.getData());
      const std::string reason = info != nullptr ? info->m_what : "connection failed";
      delete info;
      fail(reason.c_str());
    });
    m_events->addHandler(EventTypes::StreamInputReady, target, [this](const auto &) { handleInput(); });
    m_events->addHandler(EventTypes::StreamOutputError, target, [this](const auto &) { fail("output error"); });
    m_events->addHandler(EventTypes::StreamInputShutdown, target, [this](const auto &) { fail("input shutdown"); });
    m_events->addHandler(EventTypes::StreamOutputShutdown, target, [this](const auto &) { fail("output shutdown"); });
    m_events->addHandler(EventTypes::StreamInputFormatError, target, [this](const auto &) {
      fail("input format error");
    });

    m_timer = m_events->newOneShotTimer(kClipboardChannelConnectTimeout, nullptr);
    m_events->addHandler(EventTypes::Timer, m_timer, [this](const auto &) { handleTimeout(); });
    rawSocket->connect(address);
  } catch (const BaseException &e) {
    LOG_WARN("clipboard channel connection failed: %s", e.what());
    fail("connection setup failed");
  }
}

void ClipboardChannelClient::stop()
{
  clearTimer();
  removeHandlers();
  delete m_stream;
  m_stream = nullptr;
  m_token.clear();
  m_phase = Phase::Disconnected;
}

void ClipboardChannelClient::bindNetworkInterface(IDataSocket *socket) const
{
  const auto interfaceAddress = Settings::value(Settings::Core::Interface).toString();
  if (interfaceAddress.isEmpty()) {
    return;
  }

  NetworkAddress bindAddress(interfaceAddress.toStdString());
  bindAddress.resolve();
  socket->bind(bindAddress);
}

void ClipboardChannelClient::handleTransportConnected()
{
  if (m_phase != Phase::Connecting || m_stream == nullptr) {
    return;
  }
  m_phase = Phase::AwaitingHello;
  if (m_stream->isReady()) {
    m_events->addEvent(Event(EventTypes::StreamInputReady, m_stream->getEventTarget()));
  }
}

void ClipboardChannelClient::handleInput()
{
  if (m_stream == nullptr) {
    return;
  }

  if (m_phase == Phase::AwaitingHello) {
    int16_t serverMajor = 0;
    int16_t serverMinor = 0;
    std::string protocolName;
    if (!ProtocolUtil::readf(m_stream, kMsgHello, &protocolName, &serverMajor, &serverMinor) ||
        networkProtocolFromString(QString::fromStdString(protocolName)) == NetworkProtocol::Unknown ||
        serverMajor != kProtocolMajorVersion || serverMinor < 11) {
      fail("incompatible clipboard channel hello");
      return;
    }

    const auto negotiatedMinor = (std::min)(serverMinor, kProtocolMinorVersion);
    const std::string helloBackFormat = protocolName + kMsgHelloBackArgs + kMsgDClipboardChannelHello;
    ProtocolUtil::writef(
        m_stream, helloBackFormat.c_str(), kProtocolMajorVersion, negotiatedMinor, &m_clientName, &m_token
    );
    m_phase = Phase::AwaitingReady;
  } else if (m_phase == Phase::AwaitingReady) {
    uint8_t code[4]{};
    if (m_stream->read(code, sizeof(code)) != sizeof(code) ||
        std::memcmp(code, kMsgCClipboardChannelReady, sizeof(code)) != 0) {
      fail("clipboard channel was not accepted");
      return;
    }

    clearTimer();
    removeHandlers();
    auto *stream = std::exchange(m_stream, nullptr);
    m_token.clear();
    m_phase = Phase::Disconnected;
    m_connected(stream);
    return;
  }

  if (m_stream != nullptr && m_stream->isReady()) {
    m_events->addEvent(Event(EventTypes::StreamInputReady, m_stream->getEventTarget()));
  }
}

void ClipboardChannelClient::handleTimeout()
{
  fail("connection timed out");
}

void ClipboardChannelClient::fail(const char *reason)
{
  if (m_phase == Phase::Disconnected && m_stream == nullptr) {
    return;
  }
  LOG_WARN("clipboard channel unavailable: %s", reason);
  clearTimer();
  removeHandlers();
  delete m_stream;
  m_stream = nullptr;
  m_token.clear();
  m_phase = Phase::Disconnected;
  m_failed();
}

void ClipboardChannelClient::removeHandlers()
{
  if (m_stream != nullptr) {
    m_events->removeHandlers(m_stream->getEventTarget());
  }
}

void ClipboardChannelClient::clearTimer()
{
  if (m_timer != nullptr) {
    m_events->removeHandler(EventTypes::Timer, m_timer);
    m_events->deleteTimer(m_timer);
    m_timer = nullptr;
  }
}
