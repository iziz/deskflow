/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "client/ClientInfoUpdateState.h"
#include "client/ServerKeyTranslator.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ClipboardTransfer.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/KeyTypes.h"
#include "deskflow/KeyboardLayoutManager.h"

#include <array>
#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

class Client;
class ClientInfo;
class EventQueueTimer;
class IClipboard;
namespace deskflow {
class IStream;
}
class IEventQueue;

//! Proxy for server
/*!
This class acts a proxy for the server, converting calls into messages
to the server and messages from the server to calls on the client.
*/
class ServerProxy
{
public:
  /*!
  Process messages from the server on \p stream and forward to
  \p client.
  */
  ServerProxy(
      Client *client, deskflow::IStream *stream, IEventQueue *events, bool transactionalClipboard,
      bool clipboardFlowControl, bool separateClipboardChannel, bool atomicClipboardPublish, bool clipboardCompression
  );
  ServerProxy(ServerProxy const &) = delete;
  ServerProxy(ServerProxy &&) = delete;
  ~ServerProxy();

  ServerProxy &operator=(ServerProxy const &) = delete;
  ServerProxy &operator=(ServerProxy &&) = delete;

  //! @name manipulators
  //@{

  void onShapeChanged();
  void onCursorChanged();
  bool onGrabClipboard(ClipboardID);
  bool onClipboardChanged(ClipboardID, const IClipboard *, bool force = false);
  bool onClipboardChanged(ClipboardID, std::string data, bool force = false);
  void beginClipboardSend();
  void finishClipboardSend();
  void attachClipboardChannel(deskflow::IStream *adoptedStream);
  void requestClipboardChannel();

  //@}

protected:
  enum class ConnectionResult
  {
    Okay,
    Unknown,
    Disconnect
  };
  ConnectionResult parseHandshakeMessage(const uint8_t *code);
  ConnectionResult parseMessage(const uint8_t *code);

private:
  // if compressing mouse motion then send the last motion now
  void flushCompressedMouse();

  void sendInfo(const ClientInfo &);

  void resetKeepAliveAlarm();
  void setKeepAliveRate(double);
  void extendKeepAliveForClipboardTransfer(bool &extended);
  void restoreKeepAliveAfterClipboardTransfer(bool &extended);
  void extendKeepAliveForClipboardIncomingTransfer();
  void restoreKeepAliveAfterClipboardIncomingTransfer();
  void extendKeepAliveForClipboardOutgoingTransfer();
  void restoreKeepAliveAfterClipboardOutgoingTransfer();

  void rememberClipboardQueued(ClipboardID id, uint32_t sequence, size_t size, bool force);
  void markClipboardTransferStarted(const ClipboardTransferAction &action);
  void markClipboardTransferEndSent(const ClipboardTransferAction &action);
  void markClipboardTransferOutputFlushed();
  void markClipboardTransferAcknowledged(uint32_t transferId);
  void markClipboardTransferCancelled(const ClipboardTransferAction &action);
  void markClipboardIncomingStarted(ClipboardID id, uint32_t sequence, uint32_t transferId, const std::string &size);
  void markClipboardIncomingCompleted(ClipboardID id, uint32_t sequence, uint32_t transferId, size_t size);
  void clearClipboardIncomingTiming(uint32_t transferId);
  deskflow::IStream *clipboardStream() const;
  void handleClipboardChannelData();
  void handleClipboardChannelFailure(const char *reason);
  void removeClipboardChannelHandlers();
  void receiveClipboardChannelOffer();
  void handleClipboardChannelRequestTimer();
  void clearClipboardChannelRequestTimer();

  void sendClipboardActions(std::vector<ClipboardTransferAction> actions, bool restoreKeepAliveWhenIdle = true);
  void handleInputProgress();
  void handleClipboardOutputFlushed();
  void handleClipboardOutgoingTimeout();
  void handleClipboardIncomingTimeout();
  void armClipboardOutgoingTimer();
  void clearClipboardOutgoingTimer();
  void armClipboardIncomingTimer();
  void clearClipboardIncomingTimer();

  // event handlers
  void handleData();
  void handleKeepAliveAlarm();

  // message handlers
  void enter();
  void leave();
  void setClipboard();
  void setClipboardTransfer();
  void acknowledgeClipboardTransfer();
  void cancelClipboardTransfer();
  void progressClipboardTransfer();
  void sendClipboardAck(uint32_t transferId);
  void sendClipboardCancel(uint32_t transferId, ClipboardTransferCancelReason reason);
  void sendClipboardProgress(uint32_t transferId, uint32_t receivedSize);
  void grabClipboard();
  void keyDown(uint16_t id, uint16_t mask, uint16_t button, const std::string &lang);
  void keyRepeat();
  void keyUp();
  void mouseDown();
  void mouseUp();
  void mouseMove();
  void mouseRelativeMove();
  void mouseWheel();
  void screensaver();
  void resetOptions();
  void setOptions();
  void queryInfo(ClientInfoUpdateState::Kind kind = ClientInfoUpdateState::Kind::Query);
  void infoAcknowledgment();
  void secureInputNotification();
  void setServerLanguages();
  void setActiveServerLanguage(const std::string_view &language);

private:
  using MessageParser = ConnectionResult (ServerProxy::*)(const uint8_t *);

  Client *m_client = nullptr;
  deskflow::IStream *m_stream = nullptr;

  uint32_t m_seqNum = 0;

  bool m_compressMouse = false;
  bool m_compressMouseRelative = false;
  int32_t m_xMouse = 0;
  int32_t m_yMouse = 0;
  int32_t m_dxMouse = 0;
  int32_t m_dyMouse = 0;

  ClientInfoUpdateState m_infoUpdateState;

  ServerKeyTranslator m_keyTranslator;

  double m_keepAliveAlarm = 0.0;
  EventQueueTimer *m_keepAliveAlarmTimer = nullptr;
  double m_savedKeepAliveAlarm = 0.0;
  bool m_clipboardIncomingKeepAliveExtended = false;
  bool m_clipboardOutgoingKeepAliveExtended = false;

  bool m_transactionalClipboard = false;
  bool m_clipboardFlowControl = false;
  bool m_separateClipboardChannel = false;
  bool m_atomicClipboardPublish = false;
  bool m_clipboardCompression = false;
  deskflow::IStream *m_clipboardStream = nullptr;
  EventQueueTimer *m_clipboardChannelRequestTimer = nullptr;
  bool m_clipboardChannelRequestOutstanding = false;
  double m_clipboardChannelRequestDelay = 0.25;
  ClipboardTransferQueue m_clipboardOutgoing;
  ClipboardTransferAssembler m_clipboardIncoming;
  size_t m_clipboardIncomingProgress = 0;
  ClipboardChunkAssembler m_legacyClipboardIncoming;
  uint64_t m_legacyClipboardGeneration[kClipboardEnd]{};

  struct ClipboardQueuedTiming
  {
    bool active = false;
    uint32_t sequence = 0;
    size_t size = 0;
    std::chrono::steady_clock::time_point queuedAt{};
  };

  struct ClipboardTransferTiming
  {
    bool active = false;
    ClipboardID id = kClipboardEnd;
    uint32_t sequence = 0;
    uint32_t transferId = 0;
    size_t size = 0;
    std::chrono::steady_clock::time_point queuedAt{};
    std::chrono::steady_clock::time_point startedAt{};
    std::chrono::steady_clock::time_point endedAt{};
    std::chrono::steady_clock::time_point flushedAt{};
    bool hasQueuedAt = false;
    bool hasEndedAt = false;
    bool hasFlushedAt = false;
  };

  std::array<ClipboardQueuedTiming, kClipboardEnd> m_clipboardQueuedTiming;
  ClipboardTransferTiming m_clipboardOutgoingTiming;
  ClipboardTransferTiming m_clipboardIncomingTiming;
  EventQueueTimer *m_clipboardOutgoingTimer = nullptr;
  EventQueueTimer *m_clipboardIncomingTimer = nullptr;

  MessageParser m_parser = &ServerProxy::parseHandshakeMessage;
  IEventQueue *m_events = nullptr;
  std::string m_serverLayout = "";
  bool m_isUserNotifiedAboutLayoutSyncError = false;
  deskflow::KeyboardLayoutManager m_layoutManager;
};
