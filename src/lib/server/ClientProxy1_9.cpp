/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClientProxy1_9.h"

#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/Clipboard.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"
#include "server/Server.h"

#include <cstring>

namespace {
constexpr double kClipboardTransferHeartbeatAlarm = 60.0;
}

ClientProxy1_9::ClientProxy1_9(const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events)
    : ClientProxy1_9(name, stream, server, events, false)
{
}

ClientProxy1_9::ClientProxy1_9(
    const std::string &name, deskflow::IStream *stream, Server *server, IEventQueue *events, bool clipboardFlowControl,
    bool separateClipboardChannel
)
    : ClientProxy1_8(name, stream, server, events),
      m_events(events),
      m_clipboardFlowControl(clipboardFlowControl),
      m_separateClipboardChannel(separateClipboardChannel),
      m_outgoing(0, clipboardFlowControl)
{
  if (!m_separateClipboardChannel) {
    m_events->addHandler(EventTypes::StreamOutputFlushed, getStream()->getEventTarget(), [this](const auto &) {
      handleOutputFlushed();
    });
  }
}

ClientProxy1_9::~ClientProxy1_9()
{
  clearOutgoingTimer();
  clearIncomingTimer();
  if (m_separateClipboardChannel) {
    closeClipboardStream();
  } else {
    m_events->removeHandler(EventTypes::StreamOutputFlushed, getStream()->getEventTarget());
  }
}

void ClientProxy1_9::setClipboard(ClipboardID id, const IClipboard *clipboard, uint32_t revision)
{
  if (id >= kClipboardEnd || !m_clipboard[id].m_dirty) {
    return;
  }

  m_clipboard[id].m_dirty = false;
  Clipboard::copy(&m_clipboard[id].m_clipboard, clipboard);
  auto data = m_clipboard[id].m_clipboard.marshall();
  if (data.size() <= sizeof(uint32_t)) {
    LOG_DEBUG("skipping clipboard %u transfer to \"%s\" because it has no supported formats", id, getName().c_str());
    return;
  }
  sendActions(m_outgoing.queue(id, revision, std::move(data), true));
}

void ClientProxy1_9::grabClipboard(ClipboardID id)
{
  supersedeClipboardTransfers(id);
  ClientProxy1_8::grabClipboard(id);
}

void ClientProxy1_9::supersedeClipboardTransfers(
    ClipboardID id, std::optional<uint32_t> preserveIncomingSequence
)
{
  sendActions(m_outgoing.supersede(id));

  if (m_incoming.active() && m_incoming.clipboardId() == id) {
    if (preserveIncomingSequence.has_value() && m_incoming.matches(id, *preserveIncomingSequence)) {
      LOG_DEBUG(
          "preserving clipboard transfer %u from \"%s\" for matching ownership sequence %u",
          m_incoming.transferId(), getName().c_str(), *preserveIncomingSequence
      );
      return;
    }

    const auto transferId = m_incoming.transferId();
    LOG_DEBUG(
        "cancelling clipboard transfer %u from \"%s\" because clipboard %u has a new owner", transferId,
        getName().c_str(), id
    );
    m_incoming.reset();
    m_incomingProgress = 0;
    clearIncomingTimer();
    restoreIncomingHeartbeat();
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Superseded);
  }
}

void ClientProxy1_9::beginClipboardSend()
{
  extendOutgoingHeartbeat();
}

void ClientProxy1_9::finishClipboardSend()
{
  if (!m_outgoing.active()) {
    restoreOutgoingHeartbeat();
  }
}

bool ClientProxy1_9::hasPendingClipboardPublish(ClipboardID id, uint32_t sequence) const
{
  return m_pendingClipboardPublish.matches(id, sequence);
}

void ClientProxy1_9::completeClipboardPublish(
    ClipboardID id, uint32_t sequence, std::optional<ClipboardTransferCancelReason> rejection
)
{
  const auto transferId = m_pendingClipboardPublish.resolve(id, sequence);
  if (!transferId.has_value()) {
    return;
  }

  if (rejection.has_value()) {
    LOG_INFO(
        "clipboard publication %u from \"%s\" was rejected, reason=%u", *transferId, getName().c_str(),
        static_cast<uint8_t>(*rejection)
    );
    sendClipboardCancel(*transferId, *rejection);
  } else {
    m_incoming.commitSequence(id, sequence);
    LOG_INFO("clipboard publication %u from \"%s\" was committed", *transferId, getName().c_str());
    sendClipboardAck(*transferId);
  }
}

bool ClientProxy1_9::parseMessage(const uint8_t *code)
{
  if (!m_separateClipboardChannel && parseClipboardMessage(code)) {
    return true;
  }
  if (m_separateClipboardChannel && memcmp(code, kMsgDClipboard, 4) == 0) {
    return false;
  }
  return ClientProxy1_8::parseMessage(code);
}

bool ClientProxy1_9::parseClipboardMessage(const uint8_t *code)
{
  if (memcmp(code, kMsgDClipboardTransfer, 4) == 0) {
    return recvClipboardTransfer();
  }
  if (memcmp(code, kMsgCClipboardAck, 4) == 0) {
    return recvClipboardAck();
  }
  if (memcmp(code, kMsgCClipboardCancel, 4) == 0) {
    return recvClipboardCancel();
  }
  if (m_clipboardFlowControl && memcmp(code, kMsgCClipboardProgress, 4) == 0) {
    return recvClipboardProgress();
  }
  return false;
}

bool ClientProxy1_9::adoptClipboardStream(deskflow::IStream *stream)
{
  if (!m_separateClipboardChannel || stream == nullptr || m_clipboardStream != nullptr) {
    return false;
  }

  m_clipboardStream.reset(stream);
  addClipboardStreamHandlers();
  if (m_clipboardStream->isReady()) {
    m_events->addEvent(Event(EventTypes::StreamInputReady, m_clipboardStream->getEventTarget()));
  }
  return true;
}

void ClientProxy1_9::closeClipboardStream()
{
  if (m_clipboardStream == nullptr) {
    return;
  }

  removeClipboardStreamHandlers();
  clearOutgoingTimer();
  clearIncomingTimer();
  m_outgoing.transportReset();
  m_incoming.reset();
  m_incomingProgress = 0;
  m_pendingClipboardPublish.reset();
  restoreIncomingHeartbeat();
  restoreOutgoingHeartbeat();

  auto stream = std::move(m_clipboardStream);
  stream->close();
}

bool ClientProxy1_9::hasClipboardStream() const
{
  return m_clipboardStream != nullptr;
}

deskflow::IStream *ClientProxy1_9::clipboardStream() const
{
  return m_separateClipboardChannel ? m_clipboardStream.get() : getStream();
}

void ClientProxy1_9::resumeClipboardTransport()
{
  if (clipboardStream() != nullptr) {
    sendActions(m_outgoing.transportReady());
  }
}

void ClientProxy1_9::onClipboardStreamDisconnected()
{
}

void ClientProxy1_9::addClipboardStreamHandlers()
{
  assert(m_clipboardStream != nullptr);
  auto *target = m_clipboardStream->getEventTarget();
  m_events->addHandler(EventTypes::StreamInputReady, target, [this](const auto &) { handleClipboardData(); });
  m_events->addHandler(EventTypes::StreamOutputFlushed, target, [this](const auto &) { handleOutputFlushed(); });
  m_events->addHandler(EventTypes::StreamOutputError, target, [this](const auto &) {
    handleClipboardStreamFailure("stream output error");
  });
  m_events->addHandler(EventTypes::StreamInputShutdown, target, [this](const auto &) {
    handleClipboardStreamFailure("stream input shutdown");
  });
  m_events->addHandler(EventTypes::StreamOutputShutdown, target, [this](const auto &) {
    handleClipboardStreamFailure("stream output shutdown");
  });
  m_events->addHandler(EventTypes::StreamInputFormatError, target, [this](const auto &) {
    handleClipboardStreamFailure("stream input format error");
  });
  m_events->addHandler(EventTypes::SocketDisconnected, target, [this](const auto &) {
    handleClipboardStreamFailure("socket disconnected");
  });
}

void ClientProxy1_9::removeClipboardStreamHandlers()
{
  if (m_clipboardStream == nullptr) {
    return;
  }
  using enum EventTypes;
  auto *target = m_clipboardStream->getEventTarget();
  m_events->removeHandler(StreamInputReady, target);
  m_events->removeHandler(StreamOutputFlushed, target);
  m_events->removeHandler(StreamOutputError, target);
  m_events->removeHandler(StreamInputShutdown, target);
  m_events->removeHandler(StreamOutputShutdown, target);
  m_events->removeHandler(StreamInputFormatError, target);
  m_events->removeHandler(SocketDisconnected, target);
}

void ClientProxy1_9::handleClipboardData()
{
  constexpr size_t kPacketsPerDispatch = 4;
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    return;
  }

  uint8_t code[4];
  size_t processed = 0;
  while (processed < kPacketsPerDispatch) {
    const uint32_t size = stream->read(code, sizeof(code));
    if (size == 0) {
      break;
    }
    if (size != sizeof(code) || !parseClipboardMessage(code)) {
      LOG_WARN("invalid message on clipboard channel from \"%s\"", getName().c_str());
      handleClipboardStreamFailure("invalid clipboard channel message");
      return;
    }
    ++processed;
    stream = clipboardStream();
    if (stream == nullptr) {
      return;
    }
  }

  if (stream != nullptr && stream->isReady()) {
    m_events->addEvent(Event(EventTypes::StreamInputReady, stream->getEventTarget()));
  }
}

void ClientProxy1_9::handleClipboardStreamFailure(const char *reason)
{
  if (m_clipboardStream == nullptr) {
    return;
  }

  LOG_WARN("clipboard channel for \"%s\" disconnected: %s", getName().c_str(), reason != nullptr ? reason : "unknown");
  closeClipboardStream();
  onClipboardStreamDisconnected();
}

void ClientProxy1_9::sendActions(std::vector<ClipboardTransferAction> actions, bool restoreHeartbeatWhenIdle)
{
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    // queue() may already have promoted the latest snapshot to active. Put
    // it back into pending state until a replacement transport is attached.
    m_outgoing.transportReset();
    return;
  }

  for (auto &action : actions) {
    switch (action.type) {
    case ClipboardTransferActionType::Start:
    case ClipboardTransferActionType::Data:
    case ClipboardTransferActionType::End: {
      const uint8_t mark = action.type == ClipboardTransferActionType::Start  ? ChunkType::DataStart
                           : action.type == ClipboardTransferActionType::Data ? ChunkType::DataChunk
                                                                              : ChunkType::DataEnd;
      ProtocolUtil::writef(
          stream, kMsgDClipboardTransfer, action.clipboardId, action.sequence, action.transferId, mark, &action.data
      );
      extendOutgoingHeartbeat();
      if (action.type == ClipboardTransferActionType::Start) {
        LOG_DEBUG(
            "starting clipboard transfer %u to \"%s\", size=%s", action.transferId, getName().c_str(),
            action.data.c_str()
        );
      } else if (action.type == ClipboardTransferActionType::End) {
        LOG_DEBUG(
            "finished sending clipboard transfer %u to \"%s\"; waiting for acknowledgment", action.transferId,
            getName().c_str()
        );
      }
      armOutgoingTimer();
      break;
    }

    case ClipboardTransferActionType::Cancel:
      LOG_DEBUG(
          "cancelling clipboard transfer %u to \"%s\", reason=%u", action.transferId, getName().c_str(),
          static_cast<uint8_t>(action.cancelReason)
      );
      sendClipboardCancel(action.transferId, action.cancelReason);
      break;
    }
  }

  if (!m_outgoing.active()) {
    clearOutgoingTimer();
    if (restoreHeartbeatWhenIdle) {
      restoreOutgoingHeartbeat();
    }
  }
}

void ClientProxy1_9::handleOutputFlushed()
{
  if (!m_outgoing.active()) {
    clearOutgoingTimer();
    restoreOutgoingHeartbeat();
    return;
  }

  extendOutgoingHeartbeat();

  auto actions = m_outgoing.outputFlushed();
  if (actions.empty()) {
    armOutgoingTimer();
  } else {
    sendActions(std::move(actions));
  }
}

void ClientProxy1_9::handleOutgoingTimeout()
{
  if (m_separateClipboardChannel) {
    handleClipboardStreamFailure("outgoing transfer timed out");
    return;
  }

  clearOutgoingTimer();
  LOG_WARN("clipboard transfer %u timed out while sending to \"%s\"", m_outgoing.activeTransferId(), getName().c_str());
  sendActions(m_outgoing.timedOut(), false);
}

void ClientProxy1_9::handleIncomingTimeout()
{
  if (m_separateClipboardChannel) {
    handleClipboardStreamFailure("incoming transfer timed out");
    return;
  }

  clearIncomingTimer();
  if (m_incoming.active()) {
    const auto transferId = m_incoming.transferId();
    LOG_WARN("clipboard transfer %u from \"%s\" timed out", transferId, getName().c_str());
    m_incoming.reset();
    m_incomingProgress = 0;
    restoreIncomingHeartbeat();
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Timeout);
  }
}

void ClientProxy1_9::handleInputProgress()
{
  ClientProxy1_8::handleInputProgress();

  if (!m_incoming.active()) {
    restoreIncomingHeartbeat();
  }
  if (!m_outgoing.active()) {
    restoreOutgoingHeartbeat();
  }
}

void ClientProxy1_9::armOutgoingTimer()
{
  clearOutgoingTimer();
  m_outgoingTimer = m_events->newOneShotTimer(kClipboardTransferInactivityTimeout, nullptr);
  m_events->addHandler(EventTypes::Timer, m_outgoingTimer, [this](const auto &) { handleOutgoingTimeout(); });
}

void ClientProxy1_9::clearOutgoingTimer()
{
  if (m_outgoingTimer != nullptr) {
    auto *timer = m_outgoingTimer;
    m_outgoingTimer = nullptr;
    m_events->removeHandler(EventTypes::Timer, timer);
    m_events->deleteTimer(timer);
  }
}

void ClientProxy1_9::armIncomingTimer()
{
  clearIncomingTimer();
  m_incomingTimer = m_events->newOneShotTimer(kClipboardTransferInactivityTimeout, nullptr);
  m_events->addHandler(EventTypes::Timer, m_incomingTimer, [this](const auto &) { handleIncomingTimeout(); });
}

void ClientProxy1_9::clearIncomingTimer()
{
  if (m_incomingTimer != nullptr) {
    auto *timer = m_incomingTimer;
    m_incomingTimer = nullptr;
    m_events->removeHandler(EventTypes::Timer, timer);
    m_events->deleteTimer(timer);
  }
}

void ClientProxy1_9::extendClipboardHeartbeat(bool &extended)
{
  if (m_separateClipboardChannel) {
    return;
  }
  const double currentAlarm = heartbeatAlarm();
  if (currentAlarm <= 0.0) {
    return;
  }
  if (!m_incomingHeartbeatExtended && !m_outgoingHeartbeatExtended) {
    m_savedHeartbeatAlarm = currentAlarm;
  }
  extended = true;
  if (currentAlarm < kClipboardTransferHeartbeatAlarm) {
    setHeartbeatAlarm(kClipboardTransferHeartbeatAlarm);
  } else {
    resetHeartbeatTimer();
  }
}

void ClientProxy1_9::restoreClipboardHeartbeat(bool &extended)
{
  if (m_separateClipboardChannel) {
    extended = false;
    return;
  }
  if (!extended) {
    return;
  }
  extended = false;
  if (m_incomingHeartbeatExtended || m_outgoingHeartbeatExtended) {
    const auto alarm = m_savedHeartbeatAlarm > kClipboardTransferHeartbeatAlarm ? m_savedHeartbeatAlarm
                                                                                : kClipboardTransferHeartbeatAlarm;
    setHeartbeatAlarm(alarm);
    return;
  }

  setHeartbeatAlarm(m_savedHeartbeatAlarm);
  m_savedHeartbeatAlarm = 0.0;
}

void ClientProxy1_9::extendIncomingHeartbeat()
{
  extendClipboardHeartbeat(m_incomingHeartbeatExtended);
}

void ClientProxy1_9::restoreIncomingHeartbeat()
{
  restoreClipboardHeartbeat(m_incomingHeartbeatExtended);
}

void ClientProxy1_9::extendOutgoingHeartbeat()
{
  extendClipboardHeartbeat(m_outgoingHeartbeatExtended);
}

void ClientProxy1_9::restoreOutgoingHeartbeat()
{
  restoreClipboardHeartbeat(m_outgoingHeartbeatExtended);
}

bool ClientProxy1_9::recvClipboardTransfer()
{
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    return false;
  }
  ClipboardID id;
  uint32_t sequence;
  uint32_t transferId;
  uint8_t mark;
  std::string data;
  if (!ProtocolUtil::readf(stream, kMsgDClipboardTransfer + 4, &id, &sequence, &transferId, &mark, &data)) {
    return false;
  }

  const bool wasCurrent = m_incoming.active() && m_incoming.transferId() == transferId;
  auto result = m_incoming.process(id, sequence, transferId, mark, data, m_server->getMaximumClipboardSizeBytes());
  if (result.replacedTransferId != 0) {
    sendClipboardCancel(result.replacedTransferId, ClipboardTransferCancelReason::Superseded);
  }

  switch (result.status) {
  case ClipboardTransferReceiveStatus::Started:
    LOG_DEBUG("receiving clipboard transfer %u from \"%s\", size=%s", transferId, getName().c_str(), data.c_str());
    extendIncomingHeartbeat();
    m_incomingProgress = 0;
    armIncomingTimer();
    return true;

  case ClipboardTransferReceiveStatus::InProgress:
    extendIncomingHeartbeat();
    armIncomingTimer();
    if (m_clipboardFlowControl && m_incoming.data().size() - m_incomingProgress >= kClipboardTransferWindowSize) {
      m_incomingProgress = m_incoming.data().size();
      sendClipboardProgress(transferId, static_cast<uint32_t>(m_incomingProgress));
    }
    return true;

  case ClipboardTransferReceiveStatus::Finished: {
    clearIncomingTimer();
    restoreIncomingHeartbeat();
    try {
      Clipboard clipboard;
      if (!clipboard.unmarshall(m_incoming.data(), 0)) {
        m_incoming.reset();
        m_incomingProgress = 0;
        sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
        return true;
      }
      if (usesAtomicClipboardPublish() && m_pendingClipboardPublish.active()) {
        m_incoming.reset();
        m_incomingProgress = 0;
        sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
        return true;
      }
      Clipboard::copy(&m_clipboard[id].m_clipboard, &clipboard);
    } catch (...) {
      m_incoming.reset();
      m_incomingProgress = 0;
      sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
      return true;
    }
    m_clipboard[id].m_sequenceNumber = sequence;

    if (usesAtomicClipboardPublish()) {
      if (!m_pendingClipboardPublish.begin(id, sequence, transferId)) {
        m_incoming.reset();
        m_incomingProgress = 0;
        sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
        return true;
      }
    } else {
      m_incoming.commitSequence(id, sequence);
    }
    auto *info = new ClipboardInfo;
    info->m_id = id;
    info->m_sequenceNumber = sequence;
    m_events->addEvent(Event(EventTypes::ClipboardChanged, getEventTarget(), info));
    m_incoming.reset();
    m_incomingProgress = 0;
    if (usesAtomicClipboardPublish()) {
      LOG_INFO("clipboard transfer %u from \"%s\" completed; waiting for server commit", transferId, getName().c_str());
    } else {
      sendClipboardAck(transferId);
      LOG_INFO("clipboard transfer %u from \"%s\" completed", transferId, getName().c_str());
    }
    return true;
  }

  case ClipboardTransferReceiveStatus::Error:
    if (wasCurrent || !m_incoming.active()) {
      clearIncomingTimer();
      restoreIncomingHeartbeat();
      m_incoming.reset();
      m_incomingProgress = 0;
    }
    sendClipboardCancel(transferId, ClipboardTransferCancelReason::Invalid);
    return true;

  case ClipboardTransferReceiveStatus::Ignored:
    return true;
  }
  return true;
}

bool ClientProxy1_9::recvClipboardAck()
{
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    return false;
  }
  uint32_t transferId;
  if (!ProtocolUtil::readf(stream, kMsgCClipboardAck + 4, &transferId)) {
    return false;
  }
  if (m_outgoing.activeTransferId() == transferId) {
    LOG_DEBUG("clipboard transfer %u to \"%s\" was acknowledged", transferId, getName().c_str());
    clearOutgoingTimer();
    sendActions(m_outgoing.acknowledged(transferId));
  }
  return true;
}

bool ClientProxy1_9::recvClipboardCancel()
{
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    return false;
  }
  uint32_t transferId;
  uint8_t reason;
  if (!ProtocolUtil::readf(stream, kMsgCClipboardCancel + 4, &transferId, &reason)) {
    return false;
  }

  if (m_incoming.active() && m_incoming.transferId() == transferId) {
    LOG_DEBUG("clipboard transfer %u from \"%s\" was cancelled, reason=%u", transferId, getName().c_str(), reason);
    m_incoming.cancel(transferId);
    m_incomingProgress = 0;
    clearIncomingTimer();
    restoreIncomingHeartbeat();
  }
  m_pendingClipboardPublish.cancel(transferId);

  if (m_outgoing.activeTransferId() == transferId) {
    const auto cancelReason = reason >= static_cast<uint8_t>(ClipboardTransferCancelReason::Superseded) &&
                                      reason <= static_cast<uint8_t>(ClipboardTransferCancelReason::Invalid)
                                  ? static_cast<ClipboardTransferCancelReason>(reason)
                                  : ClipboardTransferCancelReason::Invalid;
    clearOutgoingTimer();
    sendActions(m_outgoing.cancelled(transferId, cancelReason));
  }
  return true;
}

bool ClientProxy1_9::recvClipboardProgress()
{
  auto *stream = clipboardStream();
  if (stream == nullptr) {
    return false;
  }
  uint32_t transferId;
  uint32_t receivedSize;
  if (!ProtocolUtil::readf(stream, kMsgCClipboardProgress + 4, &transferId, &receivedSize)) {
    return false;
  }

  auto actions = m_outgoing.progressAcknowledged(transferId, receivedSize);
  if (!actions.empty()) {
    clearOutgoingTimer();
    sendActions(std::move(actions));
  }
  return true;
}

void ClientProxy1_9::sendClipboardAck(uint32_t transferId)
{
  if (auto *stream = clipboardStream(); stream != nullptr) {
    ProtocolUtil::writef(stream, kMsgCClipboardAck, transferId);
  }
}

void ClientProxy1_9::sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason)
{
  if (auto *stream = clipboardStream(); stream != nullptr) {
    ProtocolUtil::writef(stream, kMsgCClipboardCancel, transferId, static_cast<uint8_t>(reason));
  }
}

void ClientProxy1_9::sendClipboardProgress(uint32_t transferId, uint32_t receivedSize)
{
  if (auto *stream = clipboardStream(); stream != nullptr) {
    ProtocolUtil::writef(stream, kMsgCClipboardProgress, transferId, receivedSize);
  }
}
