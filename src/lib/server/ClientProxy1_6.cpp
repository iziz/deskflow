/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_6.h"

#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ProtocolUtil.h"
#include "deskflow/StreamChunker.h"
#include "io/IStream.h"
#include "server/Server.h"

namespace {
constexpr double kClipboardTransferHeartbeatAlarm = 60.0;
}

//
// ClientProxy1_6
//

ClientProxy1_6::ClientProxy1_6(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_5(name, stream, server, events),
      m_events(events)
{
  m_events->addHandler(EventTypes::ClipboardSending, this, [this](const auto &e) {
    auto *chunk = dynamic_cast<ClipboardChunk *>(e.getDataObject());
    if (chunk != nullptr && chunk->clipboardId() < kClipboardEnd &&
        chunk->generation() == m_legacyClipboardGeneration[chunk->clipboardId()]) {
      ClipboardChunk::send(getStream(), chunk);
      extendHeartbeatForClipboardOutgoingTransfer();
    }
  });
}

ClientProxy1_6::~ClientProxy1_6()
{
  m_events->removeHandler(EventTypes::ClipboardSending, this);
}

void ClientProxy1_6::setClipboard(ClipboardID id, const IClipboard *clipboard, uint32_t revision)
{
  // ignore if this clipboard is already clean
  if (m_clipboard[id].m_dirty) {
    // this clipboard is now clean
    m_clipboard[id].m_dirty = false;
    Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);

    std::string data = m_clipboard[id].m_clipboard.marshall();
    if (data.size() <= sizeof(uint32_t)) {
      LOG_DEBUG("skipping clipboard %d transfer to \"%s\" because it has no supported formats", id, getName().c_str());
      return;
    }

    size_t size = data.size();
    LOG_DEBUG("sending clipboard %d to \"%s\"", id, getName().c_str());

    ++m_legacyClipboardGeneration[id];
    m_legacyClipboardOutgoingActive = true;
    extendHeartbeatForClipboardOutgoingTransfer();
    StreamChunker::sendClipboard(data, size, id, revision, m_events, this, m_legacyClipboardGeneration[id]);
  }
}

void ClientProxy1_6::supersedeClipboardTransfers(ClipboardID id, std::optional<uint32_t>)
{
  if (id >= kClipboardEnd) {
    return;
  }

  ++m_legacyClipboardGeneration[id];
  if (m_legacyClipboardIncoming.active() && m_legacyClipboardIncoming.clipboardId() == id) {
    m_legacyClipboardIncoming.reset();
    restoreHeartbeatAfterClipboardIncomingTransfer();
  }
}

void ClientProxy1_6::beginClipboardSend()
{
  extendHeartbeatForClipboardOutgoingTransfer();
}

void ClientProxy1_6::finishClipboardSend()
{
  if (!m_legacyClipboardOutgoingActive) {
    restoreHeartbeatAfterClipboardOutgoingTransfer();
  }
}

void ClientProxy1_6::extendHeartbeatForClipboardTransfer(bool &extended)
{
  const double currentAlarm = heartbeatAlarm();
  if (currentAlarm <= 0.0) {
    return;
  }
  if (!m_clipboardIncomingHeartbeatExtended && !m_clipboardOutgoingHeartbeatExtended) {
    m_savedHeartbeatAlarm = currentAlarm;
  }

  extended = true;
  if (currentAlarm < kClipboardTransferHeartbeatAlarm) {
    setHeartbeatAlarm(kClipboardTransferHeartbeatAlarm);
  } else {
    resetHeartbeatTimer();
  }
}

void ClientProxy1_6::restoreHeartbeatAfterClipboardTransfer(bool &extended)
{
  if (!extended) {
    return;
  }

  extended = false;
  if (m_clipboardIncomingHeartbeatExtended || m_clipboardOutgoingHeartbeatExtended) {
    const auto alarm = m_savedHeartbeatAlarm > kClipboardTransferHeartbeatAlarm ? m_savedHeartbeatAlarm
                                                                                : kClipboardTransferHeartbeatAlarm;
    setHeartbeatAlarm(alarm);
    return;
  }

  setHeartbeatAlarm(m_savedHeartbeatAlarm);
  m_savedHeartbeatAlarm = 0.0;
}

void ClientProxy1_6::extendHeartbeatForClipboardIncomingTransfer()
{
  extendHeartbeatForClipboardTransfer(m_clipboardIncomingHeartbeatExtended);
}

void ClientProxy1_6::restoreHeartbeatAfterClipboardIncomingTransfer()
{
  restoreHeartbeatAfterClipboardTransfer(m_clipboardIncomingHeartbeatExtended);
}

void ClientProxy1_6::extendHeartbeatForClipboardOutgoingTransfer()
{
  extendHeartbeatForClipboardTransfer(m_clipboardOutgoingHeartbeatExtended);
}

void ClientProxy1_6::restoreHeartbeatAfterClipboardOutgoingTransfer()
{
  restoreHeartbeatAfterClipboardTransfer(m_clipboardOutgoingHeartbeatExtended);
}

void ClientProxy1_6::handleInputProgress()
{
  ClientProxy1_0::handleInputProgress();
  m_legacyClipboardOutgoingActive = false;
  restoreHeartbeatAfterClipboardOutgoingTransfer();
}

bool ClientProxy1_6::recvClipboard()
{
  // parse message
  ClipboardID id;
  uint32_t seq;

  auto r = m_legacyClipboardIncoming.process(getStream(), id, seq, m_server->getMaximumClipboardSizeBytes());
  if (r == TransferState::Started) {
    extendHeartbeatForClipboardIncomingTransfer();
    LOG_DEBUG("receiving clipboard %d size=%zu", id, m_legacyClipboardIncoming.expectedSize());
  } else if (r == TransferState::InProgress) {
    extendHeartbeatForClipboardIncomingTransfer();
  } else if (r == TransferState::Finished) {
    restoreHeartbeatAfterClipboardIncomingTransfer();
    LOG(
        (CLOG_DEBUG "received client \"%s\" clipboard %d seqnum=%d, size=%zu", getName().c_str(), id, seq,
         m_legacyClipboardIncoming.data().size())
    );
    // save clipboard
    if (!m_clipboard[id].m_clipboard.unmarshall(m_legacyClipboardIncoming.data(), 0)) {
      m_legacyClipboardIncoming.reset();
      LOG_WARN("ignored invalid clipboard update from client \"%s\"", getName().c_str());
      return true;
    }
    m_clipboard[id].m_sequenceNumber = seq;

    // notify
    auto *info = new ClipboardInfo;
    info->m_id = id;
    info->m_sequenceNumber = seq;
    m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
    m_legacyClipboardIncoming.reset();
  } else if (r == TransferState::Error) {
    restoreHeartbeatAfterClipboardIncomingTransfer();
    return false;
  }

  return true;
}
