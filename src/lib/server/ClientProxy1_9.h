/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTransfer.h"
#include "server/ClientProxy1_8.h"

#include <memory>

class EventQueueTimer;

class ClientProxy1_9 : public ClientProxy1_8
{
public:
  ClientProxy1_9(const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events);
  ~ClientProxy1_9() override;

  void setClipboard(ClipboardID id, const IClipboard *clipboard, uint32_t revision = 0) override;
  void grabClipboard(ClipboardID id) override;
  void supersedeClipboardTransfers(
      ClipboardID id, std::optional<uint32_t> preserveIncomingSequence = std::nullopt
  ) override;
  void beginClipboardSend() override;
  void finishClipboardSend() override;
  bool parseMessage(const uint8_t *code) override;

protected:
  ClientProxy1_9(
      const std::string &name, deskflow::IStream *adoptedStream, Server *server, IEventQueue *events,
      bool clipboardFlowControl, bool separateClipboardChannel = false
  );

  bool adoptClipboardStream(deskflow::IStream *stream);
  void closeClipboardStream();
  bool hasClipboardStream() const;
  deskflow::IStream *clipboardStream() const;
  void resumeClipboardTransport();

  virtual void onClipboardStreamDisconnected();

private:
  bool parseClipboardMessage(const uint8_t *code);
  void sendActions(std::vector<ClipboardTransferAction> actions, bool restoreHeartbeatWhenIdle = true);
  void handleOutputFlushed();
  void handleClipboardData();
  void handleClipboardStreamFailure(const char *reason);
  void addClipboardStreamHandlers();
  void removeClipboardStreamHandlers();
  void handleOutgoingTimeout();
  void handleIncomingTimeout();
  void handleInputProgress() override;
  void armOutgoingTimer();
  void clearOutgoingTimer();
  void armIncomingTimer();
  void clearIncomingTimer();
  void extendClipboardHeartbeat(bool &extended);
  void restoreClipboardHeartbeat(bool &extended);
  void extendIncomingHeartbeat();
  void restoreIncomingHeartbeat();
  void extendOutgoingHeartbeat();
  void restoreOutgoingHeartbeat();

  bool recvClipboardTransfer();
  bool recvClipboardAck();
  bool recvClipboardCancel();
  bool recvClipboardProgress();
  void sendClipboardAck(uint32_t transferId);
  void sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason);
  void sendClipboardProgress(uint32_t transferId, uint32_t receivedSize);

  IEventQueue *m_events;
  bool m_clipboardFlowControl = false;
  bool m_separateClipboardChannel = false;
  std::unique_ptr<deskflow::IStream> m_clipboardStream;
  ClipboardTransferQueue m_outgoing;
  ClipboardTransferAssembler m_incoming;
  size_t m_incomingProgress = 0;
  EventQueueTimer *m_outgoingTimer = nullptr;
  EventQueueTimer *m_incomingTimer = nullptr;
  bool m_incomingHeartbeatExtended = false;
  bool m_outgoingHeartbeatExtended = false;
  double m_savedHeartbeatAlarm = 0.0;
};
