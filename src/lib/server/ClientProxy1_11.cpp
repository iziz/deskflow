/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_11.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"

#include <QRandomGenerator>

#include <array>
#include <cstring>

namespace {
constexpr auto kClipboardChannelTokenLifetime = std::chrono::seconds(30);
} // namespace

ClientProxy1_11::ClientProxy1_11(
    const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events
)
    : ClientProxy1_10(name, adoptedStream, server, events, true)
{
}

ClientProxy1_11::~ClientProxy1_11()
{
  m_destroying = true;
  m_channelToken.clear();
  closeClipboardStream();
}

void ClientProxy1_11::offerClipboardChannel()
{
  if (m_destroying) {
    return;
  }

  // A request for a replacement offer invalidates the old transport and
  // token. Active clipboard data is retained by the transfer queue and will
  // restart with a fresh transfer ID after the new channel is ready.
  closeClipboardStream();
  m_channelToken.issue(
      makeChannelToken(), deskflow::ClipboardChannelToken::Clock::now() + kClipboardChannelTokenLifetime
  );
  auto channelToken = m_channelToken.value();
  ProtocolUtil::writef(getStream(), kMsgCClipboardChannelOffer, &channelToken);
  LOG_DEBUG("offered a dedicated clipboard channel to \"%s\"", getName().c_str());
}

bool ClientProxy1_11::attachClipboardChannel(const std::string &token, deskflow::IStream *stream)
{
  if (stream == nullptr || !m_channelToken.consume(token, deskflow::ClipboardChannelToken::Clock::now())) {
    return false;
  }

  // Consume before attaching so a token can never be replayed, including if
  // stream setup fails below.
  if (!adoptClipboardStream(stream)) {
    return false;
  }

  ProtocolUtil::writef(clipboardStream(), kMsgCClipboardChannelReady);
  resumeClipboardTransport();
  LOG_INFO("dedicated clipboard channel for \"%s\" is ready", getName().c_str());
  return true;
}

bool ClientProxy1_11::parseMessage(const uint8_t *code)
{
  if (std::memcmp(code, kMsgQClipboardChannel, 4) == 0) {
    offerClipboardChannel();
    return true;
  }
  return ClientProxy1_10::parseMessage(code);
}

void ClientProxy1_11::onClipboardStreamDisconnected()
{
  if (!m_destroying) {
    offerClipboardChannel();
  }
}

std::string ClientProxy1_11::makeChannelToken()
{
  std::array<quint32, kClipboardChannelTokenSize / sizeof(quint32)> words{};
  auto *generator = QRandomGenerator::system();
  for (auto &word : words) {
    word = generator->generate();
  }
  return {reinterpret_cast<const char *>(words.data()), kClipboardChannelTokenSize};
}
