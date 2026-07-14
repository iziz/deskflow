/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Deskflow Developers.
 * SPDX-FileCopyrightText: (C) 2012 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/Server.h"

#include "base/FinalAction.h"
#include "base/IEventQueue.h"
#include "base/Log.h"
#include "deskflow/AppUtil.h"
#include "deskflow/ClipboardTransfer.h"
#include "deskflow/DeskflowException.h"
#include "deskflow/IPlatformScreen.h"
#include "deskflow/OptionTypes.h"
#include "deskflow/PacketStreamFilter.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/Screen.h"
#include "deskflow/StreamChunker.h"
#include "deskflow/ipc/CoreIpc.h"
#include "net/TCPSocket.h"
#include "server/ClientListener.h"
#include "server/ClientProxy.h"
#include "server/ClientProxyUnknown.h"
#include "server/EdgeSwitchGeometry.h"
#include "server/PrimaryClient.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <optional>

using namespace deskflow::server;

namespace {

constexpr int32_t kSecondarySwitchEdgeMargin = 16;
constexpr auto kMouseMotionLogInterval = std::chrono::milliseconds(100);

bool shouldLogMouseMotion()
{
  static thread_local auto lastLogTime = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - lastLogTime < kMouseMotionLogInterval) {
    return false;
  }
  lastLogTime = now;
  return true;
}

Direction oppositeDirection(Direction direction)
{
  switch (direction) {
    using enum Direction;
  case Left:
    return Right;

  case Right:
    return Left;

  case Top:
    return Bottom;

  case Bottom:
    return Top;

  case NoDirection:
    return NoDirection;
  }

  return Direction::NoDirection;
}

bool containsPhysicalPosition(float start, float length, float position)
{
  return position >= start && position < start + length;
}

bool isCandidateInDirection(
    const Config::PhysicalScreen &src, const Config::PhysicalScreen &dst, Direction direction, float &distance
)
{
  switch (direction) {
    using enum Direction;
  case Left:
    if (dst.x + dst.width > src.x) {
      return false;
    }
    distance = src.x - (dst.x + dst.width);
    return true;

  case Right:
    if (dst.x < src.x + src.width) {
      return false;
    }
    distance = dst.x - (src.x + src.width);
    return true;

  case Top:
    if (dst.y + dst.height > src.y) {
      return false;
    }
    distance = src.y - (dst.y + dst.height);
    return true;

  case Bottom:
    if (dst.y < src.y + src.height) {
      return false;
    }
    distance = dst.y - (src.y + src.height);
    return true;

  case NoDirection:
    break;
  }

  return false;
}

bool overlapsPerpendicularAxis(
    const Config::PhysicalScreen &src, const Config::PhysicalScreen &dst, Direction direction
)
{
  switch (direction) {
    using enum Direction;
  case Left:
  case Right:
    return src.y < dst.y + dst.height && dst.y < src.y + src.height;

  case Top:
  case Bottom:
    return src.x < dst.x + dst.width && dst.x < src.x + src.width;

  case NoDirection:
    break;
  }

  return false;
}

} // namespace

//
// Server
//

Server::Server(ServerConfig &config, PrimaryClient *primaryClient, deskflow::Screen *screen, IEventQueue *events)
    : m_primaryClient(primaryClient),
      m_active(primaryClient),
      m_config(&config),
      m_inputFilter(config.getInputFilter()),
      m_screen(screen),
      m_events(events)
{
  // must have a primary client and it must have a canonical name
  assert(m_primaryClient != nullptr);
  assert(config.isScreen(primaryClient->getName()));
  assert(m_screen != nullptr);

  std::string primaryName = getName(primaryClient);
  m_clipboardPublicationAuthority.recordFocus(primaryName, m_seqNum);

  // clear clipboards
  for (auto &clipboard : m_clipboards) {
    clipboard.m_clipboardOwner = primaryName;
    if (clipboard.m_clipboard.open(0)) {
      clipboard.m_clipboard.empty();
      clipboard.m_clipboard.close();
    }
    clipboard.m_clipboardData = clipboard.m_clipboard.marshall();
  }

  // install event handlers
  m_events->addHandler(EventTypes::Timer, this, [this](const auto &) { handleSwitchWaitTimeout(); });
  m_events->addHandler(EventTypes::KeyStateKeyDown, m_inputFilter, [this](const auto &e) { handleKeyDownEvent(e); });
  m_events->addHandler(EventTypes::KeyStateKeyUp, m_inputFilter, [this](const auto &e) { handleKeyUpEvent(e); });
  m_events->addHandler(EventTypes::KeyStateKeyRepeat, m_inputFilter, [this](const auto &e) {
    handleKeyRepeatEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryScreenButtonDown, m_inputFilter, [this](const auto &e) {
    handleButtonDownEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryScreenButtonUp, m_inputFilter, [this](const auto &e) {
    handleButtonUpEvent(e);
  });
  m_events->addHandler(
      EventTypes::PrimaryScreenMotionOnPrimary, m_primaryClient->getEventTarget(),
      [this](const auto &e) { handleMotionPrimaryEvent(e); }
  );
  m_events->addHandler(
      EventTypes::PrimaryScreenMotionOnSecondary, m_primaryClient->getEventTarget(),
      [this](const auto &e) { handleMotionSecondaryEvent(e); }
  );
  m_events->addHandler(EventTypes::PrimaryScreenWheel, m_primaryClient->getEventTarget(), [this](const auto &e) {
    handleWheelEvent(e);
  });
  m_events->addHandler(
      EventTypes::PrimaryScreenSaverActivated, m_primaryClient->getEventTarget(),
      [this](const auto &) { onScreensaver(true); }
  );
  m_events->addHandler(
      EventTypes::PrimaryScreenSaverDeactivated, m_primaryClient->getEventTarget(),
      [this](const auto &) { onScreensaver(false); }
  );
  m_events->addHandler(EventTypes::ServerSwitchToScreen, m_inputFilter, [this](const auto &e) {
    handleSwitchToScreenEvent(e);
  });
  m_events->addHandler(EventTypes::ServerSwitchInDirection, m_inputFilter, [this](const auto &e) {
    handleSwitchInDirectionEvent(e);
  });
  m_events->addHandler(EventTypes::ServerToggleScreen, m_inputFilter, [this](const auto &e) {
    handleToggleScreenEvent(e);
  });
  m_events->addHandler(EventTypes::ServerKeyboardBroadcast, m_inputFilter, [this](const auto &e) {
    handleKeyboardBroadcastEvent(e);
  });
  m_events->addHandler(EventTypes::ServerLockCursorToScreen, m_inputFilter, [this](const auto &e) {
    handleLockCursorToScreenEvent(e);
  });
  m_events->addHandler(EventTypes::PrimaryScreenFakeInputBegin, m_inputFilter, [this](const auto &) {
    m_primaryClient->fakeInputBegin();
  });
  m_events->addHandler(EventTypes::PrimaryScreenFakeInputEnd, m_inputFilter, [this](const auto &) {
    m_primaryClient->fakeInputEnd();
  });

  // add connection
  addClient(m_primaryClient);

  // set initial configuration
  setConfig(config);

  // enable primary client
  m_primaryClient->enable();
  m_inputFilter->setPrimaryClient(m_primaryClient);

  if (!m_disableLockToScreen) {
    if (m_primaryClient->getToggleMask() & KeyModifierScrollLock) {
      LOG_INFO("scroll lock is on, locking cursor to screen");
      m_lockedToScreen = true;
    } else if (m_defaultLockToScreenState) {
      LOG_INFO("default screen lock is on, locking cursor to screen");
      m_lockedToScreen = true;
    }
  }

  // The initial toggle state is only available after the primary client is enabled.
  m_primaryClient->reconfigure(getActivePrimarySides());
}

Server::~Server()
{
  // remove event handlers and timers
  using enum EventTypes;
  m_events->removeHandler(KeyStateKeyDown, m_inputFilter);
  m_events->removeHandler(KeyStateKeyUp, m_inputFilter);
  m_events->removeHandler(KeyStateKeyRepeat, m_inputFilter);
  m_events->removeHandler(PrimaryScreenButtonDown, m_inputFilter);
  m_events->removeHandler(PrimaryScreenButtonUp, m_inputFilter);
  m_events->removeHandler(PrimaryScreenMotionOnPrimary, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenMotionOnSecondary, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenWheel, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenSaverActivated, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenSaverDeactivated, m_primaryClient->getEventTarget());
  m_events->removeHandler(PrimaryScreenFakeInputBegin, m_inputFilter);
  m_events->removeHandler(PrimaryScreenFakeInputEnd, m_inputFilter);
  m_events->removeHandler(Timer, this);
  stopSwitch();

  try {
    // force immediate disconnection of secondary clients
    disconnect();
  } catch (std::exception &e) { // NOSONAR
    LOG_ERR("failed to disconnect: %s", e.what());
  }

  for (auto index = m_oldClients.begin(); index != m_oldClients.end(); ++index) {
    BaseClientProxy *client = index->first;
    m_events->deleteTimer(index->second);
    m_events->removeHandler(Timer, client);
    m_events->removeHandler(ClientProxyDisconnected, client);
    delete client;
  }

  // remove input filter
  m_inputFilter->setPrimaryClient(nullptr);

  // disable and disconnect primary client
  m_primaryClient->disable();
  removeClient(m_primaryClient);
}

size_t Server::getMaximumClipboardSizeBytes() const
{
  return m_maximumClipboardSize * 1024;
}

bool Server::setConfig(const ServerConfig &config)
{
  // refuse configuration if it doesn't include the primary screen
  if (!config.isScreen(m_primaryClient->getName())) {
    return false;
  }

  // close clients that are connected but being dropped from the
  // configuration.
  closeClients(config);
  clearSwitchBackGuard();
  clearNoNeighborEdgeGuard();

  // cut over
  processOptions();

  // add ScrollLock as a hotkey to lock to the screen.  this was a
  // built-in feature in earlier releases and is now supported via
  // the user configurable hotkey mechanism.  if the user has already
  // registered ScrollLock for something else then that will win but
  // we will unfortunately generate a warning.  if the user has
  // configured a LockCursorToScreenAction then we don't add
  // ScrollLock as a hotkey.
  if (!m_disableLockToScreen && !m_config->hasLockToScreenAction()) {
    IPlatformScreen::KeyInfo *key = IPlatformScreen::KeyInfo::alloc(kKeyScrollLock, 0, 0, 0);
    InputFilter::Rule rule(new InputFilter::KeystrokeCondition(m_events, key));
    rule.adoptAction(new InputFilter::LockCursorToScreenAction(m_events), true);
    m_inputFilter->addFilterRule(rule);
  }

  // tell primary screen about reconfiguration
  m_primaryClient->reconfigure(getActivePrimarySides());

  // tell all (connected) clients about current options
  for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    sendOptions(client);
  }

  return true;
}

void Server::adoptClient(BaseClientProxy *client)
{
  assert(client != nullptr);

  // watch for client disconnection
  m_events->addHandler(EventTypes::ClientProxyDisconnected, client, [this, client](const auto &) {
    handleClientDisconnected(client);
  });

  // name must be in our configuration
  if (!m_config->isScreen(client->getName())) {
    LOG_WARN("unrecognised client name \"%s\", check server config", client->getName().c_str());
    ipcSendToClient("unrecognisedClient", QString::fromStdString(client->getName()));
    closeClient(client, kMsgEUnknown);
    return;
  }

  // add client to client list
  if (!addClient(client)) {
    // can only have one screen with a given name at any given time
    LOG_WARN("a client with name \"%s\" is already connected", getName(client).c_str());
    closeClient(client, kMsgEBusy);
    return;
  }
  LOG_DEBUG("client \"%s\" has connected", getName(client).c_str());
  ipcSendConnectionState(deskflow::core::ConnectionState::Connected);
  sendConnectedClientsIpc();

  // send configuration options to client
  sendOptions(client);
  client->offerClipboardChannel();

  // activate screen saver on new client if active on the primary screen
  if (m_activeSaver != nullptr) {
    client->screensaver(true);
  }

  // send notification
  auto *info = new Server::ScreenConnectedInfo(getName(client));
  m_events->addEvent(Event(EventTypes::ServerConnected, m_primaryClient->getEventTarget(), info));
}

bool Server::attachClipboardChannel(const std::string &clientName, const std::string &token, deskflow::IStream *stream)
{
  if (stream == nullptr) {
    return false;
  }

  auto canonicalName = m_config->getCanonicalName(clientName);
  if (canonicalName.empty()) {
    canonicalName = clientName;
  }
  const auto client = m_clients.find(canonicalName);
  if (client == m_clients.end() || client->second == m_primaryClient) {
    return false;
  }
  return client->second->attachClipboardChannel(token, stream);
}

void Server::disconnect()
{
  // close all secondary clients
  if (m_clients.size() > 1 || !m_oldClients.empty()) {
    Config emptyConfig(m_events);
    closeClients(emptyConfig);
  } else {
    m_events->addEvent(Event(EventTypes::ServerDisconnected, this));
  }
}

std::string Server::protocolString() const
{
  if (m_protocol == NetworkProtocol::Unknown)
    throw InvalidProtocolException();
  return networkProtocolToName(m_protocol).toStdString();
}

uint32_t Server::getNumClients() const
{
  return (int32_t)m_clients.size();
}

void Server::getClients(std::vector<std::string> &list) const
{
  list.clear();
  for (auto index = m_clients.begin(); index != m_clients.end(); ++index) {
    list.push_back(index->first);
  }
}

void Server::sendConnectedClientsIpc() const
{
  const auto primaryName = getName(m_primaryClient);
  QStringList clientList;
  for (const auto &[name, _] : m_clients) {
    if (name != primaryName) {
      clientList.append(QString::fromStdString(name));
    }
  }
  ipcSendToClient("connectedClients", clientList.join(","));
}

std::string Server::getName(const BaseClientProxy *client) const
{
  std::string name = m_config->getCanonicalName(client->getName());
  if (name.empty()) {
    name = client->getName();
  }
  return name;
}

uint32_t Server::getActivePrimarySides() const
{
  using enum DirectionMask;
  using enum Direction;
  uint32_t sides = 0;
  if (!isLockedToScreenServer()) {
    if (hasAnyNeighbor(m_primaryClient, Left)) {
      sides |= static_cast<int>(LeftMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Right)) {
      sides |= static_cast<int>(RightMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Top)) {
      sides |= static_cast<int>(TopMask);
    }
    if (hasAnyNeighbor(m_primaryClient, Bottom)) {
      sides |= static_cast<int>(BottomMask);
    }
  }
  return sides;
}

bool Server::isLockedToScreenServer() const
{
  return !m_disableLockToScreen && m_lockedToScreen;
}

bool Server::isLockedToScreen() const
{
  // locked if we say we're locked
  if (isLockedToScreenServer()) {
    return true;
  }

  // locked if primary says we're locked
  if (m_primaryClient->isLockedToScreen()) {
    return true;
  }

  // not locked
  return false;
}

int32_t Server::getJumpZoneSize(const BaseClientProxy *client) const
{
  if (client == m_primaryClient) {
    return m_primaryClient->getJumpZoneSize();
  } else {
    return 0;
  }
}

void Server::switchScreen(BaseClientProxy *dst, int32_t x, int32_t y, bool forScreensaver, const char *reason)
{
  assert(dst != nullptr);
  if (reason == nullptr) {
    reason = "unknown";
  }

  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  dst->getShape(dx, dy, dw, dh);

  // any of these conditions seem to trigger when the portal permission dialog
  // is visible on wayland. this was previously an assert, but that's pretty
  // annoying since it makes the mouse unusable on the server and you'll have to
  // ssh into your machine to kill it. better to just log a warning.
  if (x < dx) {
    LOG_WARN(
        "on switch, x (%d) is less than the left boundary dx (%d)", //
        x, dx
    );
  }
  if (y < dy) {
    LOG_WARN(
        "on switch, y (%d) is less than the top boundary dy (%d)", //
        y, dy
    );
  }
  if (x >= dx + dw) {
    LOG_WARN(
        "on switch, x (%d) exceeds the right boundary (dx + width = %d)", //
        x, dx + dw
    );
  }
  if (y >= dy + dh) {
    LOG_WARN(
        "on switch, y (%d) exceeds the bottom boundary (dy + height = %d)", //
        y, dy + dh
    );
  }

  assert(m_active != nullptr);

  LOG_INFO(
      "switch from \"%s\" to \"%s\" at %d,%d reason=%s%s", getName(m_active).c_str(), getName(dst).c_str(), x, y,
      reason, forScreensaver ? " screensaver=true" : ""
  );

  // stop waiting to switch
  stopSwitch();

  // record new position
  m_x = x;
  m_y = y;
  m_xDelta = 0;
  m_yDelta = 0;
  m_xDelta2 = 0;
  m_yDelta2 = 0;

  // wrapping means leaving the active screen and entering it again.
  // since that's a waste of time we skip that and just warp the
  // mouse.
  if (m_active != dst) {
    // leave active screen
    if (!m_active->leave()) {
      // cannot leave screen
      LOG_WARN("can't leave screen");
      return;
    }

    // update the primary client's clipboards if we're leaving the
    // primary screen.
    if (m_active == m_primaryClient && m_enableClipboard) {
      for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
        const ClipboardInfo &clipboard = m_clipboards[id];
        if (clipboard.m_clipboardOwner == getName(m_primaryClient)) {
          onClipboardChanged(m_primaryClient, id, clipboard.m_sourceSequence);
        }
      }
    }

#if defined(__APPLE__)
    if (dst != m_primaryClient) {
      std::string secureInputApplication = m_primaryClient->getSecureInputApp();
      if (secureInputApplication != "") {
        // display notification on the server
        m_primaryClient->secureInputNotification(secureInputApplication);
        // display notification on the client
        dst->secureInputNotification(secureInputApplication);
      }
    }
#endif

    // cut over
    m_active = dst;

    // increment enter sequence number
    ++m_seqNum;
    m_clipboardPublicationAuthority.recordFocus(getName(m_active), m_seqNum);

    // enter new screen
    m_active->enter(x, y, m_seqNum, m_primaryClient->getToggleMask(), forScreensaver);

    if (m_enableClipboard) {
      // send the clipboard data to new active screen
      for (ClipboardID id = 0; id < kClipboardEnd; ++id) {
        // Hackity hackity hack
        const auto dataSize = m_clipboards[id].m_clipboard.marshall().size();
        if (dataSize <= sizeof(uint32_t) || dataSize > (m_maximumClipboardSize * 1024)) {
          continue;
        }
        m_active->setClipboard(id, &m_clipboards[id].m_clipboard, m_clipboards[id].m_committedRevision);
      }
    }

    auto *info = new Server::SwitchToScreenInfo(m_active->getName());
    m_events->addEvent(Event(EventTypes::ServerScreenSwitched, this, info));
  } else {
    m_active->mouseMove(x, y);
  }
}

void Server::startSwitchBackGuard(
    BaseClientProxy *screen, BaseClientProxy *blockedTarget, Direction blockedDirection,
    SwitchBackGuard::TimePoint transitionStartedAt
)
{
  if (screen == nullptr || blockedTarget == nullptr || blockedDirection == Direction::NoDirection) {
    clearSwitchBackGuard();
    return;
  }

  m_switchBackGuardScreen = screen;
  m_switchBackGuardTarget = blockedTarget;
  m_switchBackGuard.arm(blockedDirection, m_x, m_y, transitionStartedAt, SwitchBackGuard::Clock::now());
  clearNoNeighborEdgeGuard();
}

void Server::updateSwitchBackGuard(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t x, int32_t y)
{
  if (m_switchBackGuardScreen == nullptr) {
    return;
  }

  if (m_active != m_switchBackGuardScreen) {
    clearSwitchBackGuard();
    return;
  }

  if (!m_switchBackGuard.isArmed()) {
    clearSwitchBackGuard();
    return;
  }

  const auto result = m_switchBackGuard.update({ax, ay, aw, ah}, x, y);
  if (!result.shouldRelease()) {
    return;
  }

  LOG_DEBUG(
      "released switch-back guard on \"%s\" away from \"%s\" on %s: reason=%s cursor=%d,%d bounds=%d,%d %dx%d "
      "toward-velocity=%.1fpx/s away=%lldms",
      getName(m_switchBackGuardScreen).c_str(), getName(m_switchBackGuardTarget).c_str(),
      Config::dirName(m_switchBackGuard.direction()), SwitchBackGuard::releaseReasonName(result.reason), x, y, ax, ay,
      aw, ah, result.towardVelocity, static_cast<long long>(result.awayDuration.count())
  );
  clearSwitchBackGuard();
}

void Server::clearSwitchBackGuard()
{
  m_switchBackGuardScreen = nullptr;
  m_switchBackGuardTarget = nullptr;
  m_switchBackGuard.clear();
}

bool Server::isSwitchBackGuardBlocked(BaseClientProxy *newScreen, Direction dir) const
{
  return m_switchBackGuardScreen != nullptr && m_active == m_switchBackGuardScreen &&
         newScreen == m_switchBackGuardTarget && dir == m_switchBackGuard.direction();
}

void Server::startNoNeighborEdgeGuard(BaseClientProxy *screen, Direction blockedDirection)
{
  if (screen == nullptr || blockedDirection == Direction::NoDirection) {
    clearNoNeighborEdgeGuard();
    return;
  }

  m_noNeighborEdgeGuardScreen = screen;
  m_noNeighborEdgeGuardDirection = blockedDirection;
}

void Server::updateNoNeighborEdgeGuard(int32_t ax, int32_t ay, int32_t aw, int32_t ah, int32_t x, int32_t y)
{
  if (m_noNeighborEdgeGuardScreen == nullptr) {
    return;
  }

  if (m_active != m_noNeighborEdgeGuardScreen) {
    clearNoNeighborEdgeGuard();
    return;
  }

  switch (m_noNeighborEdgeGuardDirection) {
    using enum Direction;
  case Left:
    if (x > ax) {
      clearNoNeighborEdgeGuard();
    }
    break;

  case Right:
    if (x < ax + aw - 1) {
      clearNoNeighborEdgeGuard();
    }
    break;

  case Top:
    if (y > ay) {
      clearNoNeighborEdgeGuard();
    }
    break;

  case Bottom:
    if (y < ay + ah - 1) {
      clearNoNeighborEdgeGuard();
    }
    break;

  case NoDirection:
    clearNoNeighborEdgeGuard();
    break;
  }
}

void Server::clearNoNeighborEdgeGuard()
{
  m_noNeighborEdgeGuardScreen = nullptr;
  m_noNeighborEdgeGuardDirection = Direction::NoDirection;
}

bool Server::isNoNeighborEdgeGuardBlocked(BaseClientProxy *screen, Direction dir) const
{
  return m_noNeighborEdgeGuardScreen != nullptr && screen == m_noNeighborEdgeGuardScreen &&
         dir == m_noNeighborEdgeGuardDirection;
}

void Server::jumpToScreen(BaseClientProxy *newScreen, const char *reason)
{
  assert(newScreen != nullptr);

  // record the current cursor position on the active screen
  m_active->setJumpCursorPos(m_x, m_y);

  // get the last cursor position on the target screen
  int32_t x;
  int32_t y;
  newScreen->getJumpCursorPos(x, y);
  avoidJumpRestoreZone(newScreen, x, y);

  switchScreen(newScreen, x, y, false, reason);
}

float Server::mapToFraction(const BaseClientProxy *client, Direction dir, int32_t x, int32_t y) const
{
  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  client->getShape(sx, sy, sw, sh);
  switch (dir) {
    using enum Direction;
  case Left:
  case Right:
    return (y - sy + 0.5f) / static_cast<float>(sh);

  case Top:
  case Bottom:
    return (x - sx + 0.5f) / static_cast<float>(sw);

  case NoDirection:
    assert(0 && "bad direction");
    break;
  }
  return 0.0f;
}

void Server::mapToPixel(const BaseClientProxy *client, Direction dir, float f, int32_t &x, int32_t &y) const
{
  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  client->getShape(sx, sy, sw, sh);
  switch (dir) {
    using enum Direction;
  case Left:
  case Right:
    y = static_cast<int32_t>(f * sh) + sy;
    break;

  case Top:
  case Bottom:
    x = static_cast<int32_t>(f * sw) + sx;
    break;

  case NoDirection:
    assert(0 && "bad direction");
    break;
  }
}

bool Server::hasAnyNeighbor(const BaseClientProxy *client, Direction dir) const
{
  assert(client != nullptr);

  return hasPhysicalNeighbor(client, dir) || m_config->hasNeighbor(getName(client), dir);
}

void Server::recordNeighborMiss(Direction dir, const NeighborMapResult &result)
{
  assert(!result.isMapped());
  if (CLOG->getFilter() >= LogLevel::Level::Verbose) {
    LOG_VERBOSE(
        "edge neighbor lookup missed: source=\"%s\" direction=%s reason=%s position=%d,%d", getName(m_active).c_str(),
        Config::dirName(dir), neighborMapStatusKeyword(result.status()).data(), result.position().x, result.position().y
    );
  }
  if (shouldCacheNeighborMiss(result.status())) {
    startNoNeighborEdgeGuard(m_active, dir);
  }
}

bool Server::hasPhysicalNeighbor(const BaseClientProxy *src, Direction direction) const
{
  const auto *srcPhysical = m_config->getPhysicalScreen(getName(src));
  if (srcPhysical == nullptr) {
    return false;
  }

  for (const auto &[dstName, dstClient] : m_clients) {
    if (dstClient == src) {
      continue;
    }

    const auto *dstPhysical = m_config->getPhysicalScreen(dstName);
    float distance = 0.0f;
    if (dstPhysical != nullptr && isCandidateInDirection(*srcPhysical, *dstPhysical, direction, distance) &&
        overlapsPerpendicularAxis(*srcPhysical, *dstPhysical, direction)) {
      return true;
    }
  }

  return false;
}

NeighborMapResult
Server::mapToPhysicalNeighbor(const BaseClientProxy *src, Direction direction, EdgeSwitchPosition requested) const
{
  EdgeLookupFacts facts{EdgeLookupKind::PhysicalLayout, false, false, false};
  const auto *srcPhysical = m_config->getPhysicalScreen(getName(src));
  if (srcPhysical == nullptr) {
    return NeighborMapResult::miss(classifyNeighborLookup(facts), requested);
  }

  int32_t sx;
  int32_t sy;
  int32_t sw;
  int32_t sh;
  src->getShape(sx, sy, sw, sh);

  const bool horizontalSwitch = direction == Direction::Left || direction == Direction::Right;
  const float positionFraction = horizontalSwitch ? (requested.y - sy + 0.5f) / static_cast<float>(sh)
                                                  : (requested.x - sx + 0.5f) / static_cast<float>(sw);
  const float physicalPosition = horizontalSwitch ? srcPhysical->y + positionFraction * srcPhysical->height
                                                  : srcPhysical->x + positionFraction * srcPhysical->width;

  const auto containsPosition = [&](const Config::PhysicalScreen &screen) {
    return horizontalSwitch ? containsPhysicalPosition(screen.y, screen.height, physicalPosition)
                            : containsPhysicalPosition(screen.x, screen.width, physicalPosition);
  };

  const auto srcName = getName(src);
  for (const auto &dstName : *m_config) {
    if (dstName == srcName) {
      continue;
    }

    const auto *dstPhysical = m_config->getPhysicalScreen(dstName);
    float distance = 0.0f;
    if (dstPhysical == nullptr || !isCandidateInDirection(*srcPhysical, *dstPhysical, direction, distance) ||
        !overlapsPerpendicularAxis(*srcPhysical, *dstPhysical, direction)) {
      continue;
    }

    observeConfiguredPhysicalCandidate(facts, containsPosition(*dstPhysical));
  }

  BaseClientProxy *bestClient = nullptr;
  const Config::PhysicalScreen *bestPhysical = nullptr;
  float bestDistance = 0.0f;
  bool found = false;

  for (const auto &[dstName, dstClient] : m_clients) {
    if (dstClient == src) {
      continue;
    }

    const auto *dstPhysical = m_config->getPhysicalScreen(dstName);
    if (dstPhysical == nullptr) {
      continue;
    }

    float distance = 0.0f;
    if (!isCandidateInDirection(*srcPhysical, *dstPhysical, direction, distance) ||
        !overlapsPerpendicularAxis(*srcPhysical, *dstPhysical, direction)) {
      continue;
    }

    if (!containsPosition(*dstPhysical) || (found && distance >= bestDistance)) {
      continue;
    }

    observeConnectedPhysicalCandidateAtPosition(facts);
    bestClient = dstClient;
    bestPhysical = dstPhysical;
    bestDistance = distance;
    found = true;
  }

  const auto status = classifyNeighborLookup(facts);
  if (status != NeighborMapStatus::Mapped) {
    return NeighborMapResult::miss(status, requested);
  }
  assert(bestClient != nullptr && bestPhysical != nullptr);
  if (bestClient == nullptr || bestPhysical == nullptr) {
    return NeighborMapResult::miss(NeighborMapStatus::TargetDisconnected, requested);
  }

  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  bestClient->getShape(dx, dy, dw, dh);
  EdgeSwitchPosition mapped = requested;

  switch (direction) {
    using enum Direction;
  case Left:
    mapped.x = dx + dw - 1;
    mapped.y = dy + static_cast<int32_t>(((physicalPosition - bestPhysical->y) / bestPhysical->height) * dh);
    break;

  case Right:
    mapped.x = dx;
    mapped.y = dy + static_cast<int32_t>(((physicalPosition - bestPhysical->y) / bestPhysical->height) * dh);
    break;

  case Top:
    mapped.x = dx + static_cast<int32_t>(((physicalPosition - bestPhysical->x) / bestPhysical->width) * dw);
    mapped.y = dy + dh - 1;
    break;

  case Bottom:
    mapped.x = dx + static_cast<int32_t>(((physicalPosition - bestPhysical->x) / bestPhysical->width) * dw);
    mapped.y = dy;
    break;

  case NoDirection:
    return NeighborMapResult::miss(NeighborMapStatus::NoConfiguredTopology, requested);
  }

  avoidJumpZone(bestClient, direction, mapped.x, mapped.y);
  LOG_VERBOSE(
      "physical layout maps \"%s\" to \"%s\" on %s at %d,%d", getName(src).c_str(), getName(bestClient).c_str(),
      Config::dirName(direction), mapped.x, mapped.y
  );
  return NeighborMapResult::mapped(*bestClient, mapped);
}

NeighborMapResult Server::getNeighbor(const BaseClientProxy *src, Direction dir, EdgeSwitchPosition requested) const
{
  // note -- must be locked on entry

  assert(src != nullptr);

  // get source screen name
  std::string srcName = getName(src);
  assert(!srcName.empty());
  LOG_VERBOSE("find neighbor on %s of \"%s\"", Config::dirName(dir), srcName.c_str());
  EdgeLookupFacts facts{
      EdgeLookupKind::LegacyLink,
      m_config->hasNeighbor(srcName, dir),
      false,
      false,
  };

  // convert position to fraction
  float t = mapToFraction(src, dir, requested.x, requested.y);

  // search for the closest neighbor that exists in direction dir
  float tTmp;
  for (;;) {
    std::string dstName(m_config->getNeighbor(srcName, dir, t, &tTmp));

    // if nothing in that direction then return nullptr. if the
    // destination is the source then we can make no more
    // progress in this direction.  since we haven't found a
    // connected neighbor we return nullptr.
    if (dstName.empty()) {
      LOG_VERBOSE("no neighbor on %s of \"%s\"", Config::dirName(dir), srcName.c_str());
      return NeighborMapResult::miss(classifyNeighborLookup(facts), requested);
    }
    facts.hasConfiguredTopology = true;
    facts.hasConfiguredTargetAtPosition = true;

    // look up neighbor cell.  if the screen is connected and
    // ready then we can stop.
    if (ClientList::const_iterator index = m_clients.find(dstName); index != m_clients.end()) {
      LOG_VERBOSE("\"%s\" is on %s of \"%s\" at %f", dstName.c_str(), Config::dirName(dir), srcName.c_str(), t);
      EdgeSwitchPosition mapped = requested;
      mapToPixel(index->second, dir, tTmp, mapped.x, mapped.y);
      facts.hasConnectedTargetAtPosition = true;
      return NeighborMapResult::mapped(*index->second, mapped);
    }

    // skip over unconnected screen
    LOG_VERBOSE("ignored \"%s\" on %s of \"%s\"", dstName.c_str(), Config::dirName(dir), srcName.c_str());
    srcName = dstName;

    // use position on skipped screen
    t = tTmp;
  }
}

NeighborMapResult Server::mapToNeighbor(BaseClientProxy *src, Direction srcSide, EdgeSwitchPosition requested) const
{
  // note -- must be locked on entry

  assert(src != nullptr);

  const bool hasPhysicalSource = m_config->getPhysicalScreen(getName(src)) != nullptr;
  NeighborMapResult physicalResult = NeighborMapResult::miss(NeighborMapStatus::NoConfiguredTopology, requested);
  if (hasPhysicalSource) {
    physicalResult = mapToPhysicalNeighbor(src, srcSide, requested);
    if (hasPhysicalNeighbor(src, srcSide) || physicalResult.isMapped()) {
      return physicalResult;
    }
  }

  // get the first neighbor
  NeighborMapResult neighbor = getNeighbor(src, srcSide, requested);
  if (!neighbor.isMapped()) {
    return NeighborMapResult::miss(mergeNeighborMiss(neighbor.status(), physicalResult.status()), requested);
  }
  BaseClientProxy *dst = neighbor.target();
  int32_t x = neighbor.position().x;
  int32_t y = neighbor.position().y;

  // get the source screen's size
  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  BaseClientProxy *lastGoodScreen = src;
  lastGoodScreen->getShape(dx, dy, dw, dh);

  // find destination screen, adjusting x or y (but not both).  the
  // searches are done in a sort of canonical screen space where
  // the upper-left corner is 0,0 for each screen.  we adjust from
  // actual to canonical position on entry to and from canonical to
  // actual on exit from the search.
  switch (srcSide) {
    using enum Direction;
  case Left:
    x -= dx;
    while (dst != nullptr) {
      lastGoodScreen = dst;
      lastGoodScreen->getShape(dx, dy, dw, dh);
      x += dw;
      if (x >= 0) {
        break;
      }
      LOG_VERBOSE("skipping over screen %s", getName(dst).c_str());
      const auto next = getNeighbor(lastGoodScreen, srcSide, {x, y});
      dst = next.target();
      if (next.isMapped()) {
        x = next.position().x;
        y = next.position().y;
      }
    }
    assert(lastGoodScreen != nullptr);
    x += dx;
    break;

  case Right:
    x -= dx;
    while (dst != nullptr) {
      x -= dw;
      lastGoodScreen = dst;
      lastGoodScreen->getShape(dx, dy, dw, dh);
      if (x < dw) {
        break;
      }
      LOG_VERBOSE("skipping over screen %s", getName(dst).c_str());
      const auto next = getNeighbor(lastGoodScreen, srcSide, {x, y});
      dst = next.target();
      if (next.isMapped()) {
        x = next.position().x;
        y = next.position().y;
      }
    }
    assert(lastGoodScreen != nullptr);
    x += dx;
    break;

  case Top:
    y -= dy;
    while (dst != nullptr) {
      lastGoodScreen = dst;
      lastGoodScreen->getShape(dx, dy, dw, dh);
      y += dh;
      if (y >= 0) {
        break;
      }
      LOG_VERBOSE("skipping over screen %s", getName(dst).c_str());
      const auto next = getNeighbor(lastGoodScreen, srcSide, {x, y});
      dst = next.target();
      if (next.isMapped()) {
        x = next.position().x;
        y = next.position().y;
      }
    }
    assert(lastGoodScreen != nullptr);
    y += dy;
    break;

  case Bottom:
    y -= dy;
    while (dst != nullptr) {
      y -= dh;
      lastGoodScreen = dst;
      lastGoodScreen->getShape(dx, dy, dw, dh);
      if (y < dh) {
        break;
      }
      LOG_VERBOSE("skipping over screen %s", getName(dst).c_str());
      const auto next = getNeighbor(lastGoodScreen, srcSide, {x, y});
      dst = next.target();
      if (next.isMapped()) {
        x = next.position().x;
        y = next.position().y;
      }
    }
    assert(lastGoodScreen != nullptr);
    y += dy;
    break;

  case NoDirection:
    assert(0 && "bad direction");
    return NeighborMapResult::miss(NeighborMapStatus::NoConfiguredTopology, requested);
  }

  // save destination screen
  assert(lastGoodScreen != nullptr);
  dst = lastGoodScreen;

  // if entering primary screen then be sure to move in far enough
  // to avoid the jump zone.  if entering a side that doesn't have
  // a neighbor (i.e. an asymmetrical side) then we don't need to
  // move inwards because that side can't provoke a jump.
  avoidJumpZone(dst, srcSide, x, y);

  return NeighborMapResult::mapped(*dst, {x, y});
}

void Server::avoidJumpZone(const BaseClientProxy *dst, Direction dir, int32_t &x, int32_t &y) const
{
  const std::string dstName(getName(dst));
  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  dst->getShape(dx, dy, dw, dh);
  float t = mapToFraction(dst, dir, x, y);
  int32_t z = getJumpZoneSize(dst);
  const int32_t minZoneSize = (dst == m_primaryClient) ? 1 : kSecondarySwitchEdgeMargin;
  if (z < minZoneSize) {
    z = minZoneSize;
  }

  // move in far enough to avoid the jump zone.  if entering a side
  // that doesn't have a neighbor (i.e. an asymmetrical side) then we
  // don't need to move inwards because that side can't provoke a jump.
  switch (dir) {
    using enum Direction;
  case Left:
    if ((!m_config->getNeighbor(dstName, Right, t, nullptr).empty() || hasPhysicalNeighbor(dst, Right)) &&
        x > dx + dw - 1 - z)
      x = dx + dw - 1 - z;
    break;

  case Right:
    if ((!m_config->getNeighbor(dstName, Left, t, nullptr).empty() || hasPhysicalNeighbor(dst, Left)) && x < dx + z)
      x = dx + z;
    break;

  case Top:
    if ((!m_config->getNeighbor(dstName, Bottom, t, nullptr).empty() || hasPhysicalNeighbor(dst, Bottom)) &&
        y > dy + dh - 1 - z)
      y = dy + dh - 1 - z;
    break;

  case Bottom:
    if ((!m_config->getNeighbor(dstName, Top, t, nullptr).empty() || hasPhysicalNeighbor(dst, Top)) && y < dy + z)
      y = dy + z;
    break;

  case NoDirection:
    assert(0 && "bad direction");
  }
}

void Server::avoidJumpRestoreZone(const BaseClientProxy *dst, int32_t &x, int32_t &y) const
{
  const std::string dstName(getName(dst));
  int32_t dx;
  int32_t dy;
  int32_t dw;
  int32_t dh;
  dst->getShape(dx, dy, dw, dh);

  const int32_t maxX = dx + dw - 1;
  const int32_t maxY = dy + dh - 1;
  int32_t z = getJumpZoneSize(dst);
  const int32_t minZoneSize = (dst == m_primaryClient) ? 1 : kSecondarySwitchEdgeMargin;
  if (z < minZoneSize) {
    z = minZoneSize;
  }

  int32_t minX = dx;
  int32_t restoreMaxX = maxX;
  int32_t minY = dy;
  int32_t restoreMaxY = maxY;

  const int32_t probeX = std::clamp(x, dx, maxX);
  const int32_t probeY = std::clamp(y, dy, maxY);
  const auto hasNeighborAt = [&](Direction dir) {
    const float t = mapToFraction(dst, dir, probeX, probeY);
    return !m_config->getNeighbor(dstName, dir, t, nullptr).empty() || hasPhysicalNeighbor(dst, dir);
  };

  if (hasNeighborAt(Direction::Left)) {
    minX = std::min(dx + z, maxX);
  }
  if (hasNeighborAt(Direction::Right)) {
    restoreMaxX = std::max(maxX - z, dx);
  }
  if (minX > restoreMaxX) {
    minX = restoreMaxX = dx + (dw / 2);
  }

  if (hasNeighborAt(Direction::Top)) {
    minY = std::min(dy + z, maxY);
  }
  if (hasNeighborAt(Direction::Bottom)) {
    restoreMaxY = std::max(maxY - z, dy);
  }
  if (minY > restoreMaxY) {
    minY = restoreMaxY = dy + (dh / 2);
  }

  const int32_t oldX = x;
  const int32_t oldY = y;
  x = std::clamp(x, minX, restoreMaxX);
  y = std::clamp(y, minY, restoreMaxY);

  if (x != oldX || y != oldY) {
    LOG_DEBUG(
        "adjusted jump restore on \"%s\" away from switch edge: old=%d,%d new=%d,%d bounds=%d,%d %dx%d margin=%d",
        dstName.c_str(), oldX, oldY, x, y, dx, dy, dw, dh, z
    );
  }
}

SwitchPolicyResult Server::applySwitchPolicy(
    BaseClientProxy *newScreen, Direction dir, int32_t x, int32_t y, int32_t xActive, int32_t yActive
)
{
  assert(newScreen != nullptr);
  LOG_VERBOSE("try to leave \"%s\" on %s", getName(m_active).c_str(), Config::dirName(dir));
  const auto finish = [&](SwitchPolicyCondition conditions) {
    const SwitchPolicyResult result{conditions};
    if (CLOG->getFilter() >= LogLevel::Level::Verbose) {
      const auto conditionKeywords = switchPolicyConditionKeywords(conditions);
      LOG_VERBOSE(
          "edge switch policy evaluated: source=\"%s\" target=\"%s\" direction=%s decision=%s conditions=%s",
          getName(m_active).c_str(), getName(newScreen).c_str(), Config::dirName(dir),
          switchPolicyDecisionKeyword(result.decision()).data(), conditionKeywords.c_str()
      );
    }
    return result;
  };

  if (isSwitchBackGuardBlocked(newScreen, dir)) {
    LOG_DEBUG(
        "blocked immediate switch back from \"%s\" to \"%s\" on %s: delta=%+d,%+d previous=%+d,%+d",
        getName(m_active).c_str(), getName(newScreen).c_str(), Config::dirName(dir), m_xDelta, m_yDelta, m_xDelta2,
        m_yDelta2
    );
    stopSwitch();
    return finish(SwitchPolicyCondition::TransitionGuard);
  }

  // should we switch or not?
  bool preventSwitch = false;
  bool allowSwitch = false;
  SwitchPolicyCondition pendingConditions = SwitchPolicyCondition::None;

  // note if the switch direction has changed.  save the new
  // direction and screen if so.
  const bool isNewDirection = (dir != m_switchDir);
  const bool isNewTarget = (newScreen != m_switchScreen);
  if (isNewDirection || isNewTarget) {
    m_switchDir = dir;
    m_switchScreen = newScreen;
  }

  // is this a double tap and do we care?
  if (!allowSwitch && m_switchTwoTapDelay > 0.0) {
    if (isNewDirection || isNewTarget || !isSwitchTwoTapStarted() || !shouldSwitchTwoTap()) {
      // tapping a different or new edge or second tap not
      // fast enough.  prepare for second tap.
      preventSwitch = true;
      pendingConditions |= SwitchPolicyCondition::DoubleTapPending;
      startSwitchTwoTap();
    } else {
      // got second tap
      allowSwitch = true;
    }
  }

  // if waiting before a switch then prepare to switch later
  if (!allowSwitch && m_switchWaitDelay > 0.0) {
    if (isNewDirection || isNewTarget || !isSwitchWaitStarted()) {
      startSwitchWait(x, y);
    }
    preventSwitch = true;
    pendingConditions |= SwitchPolicyCondition::WaitDelayPending;
  }

  // are we in a locked corner?  first check if screen has the option set
  // and, if not, check the global options.
  const Config::ScreenOptions *options = m_config->getOptions(getName(m_active));
  if (options == nullptr || !options->contains(kOptionScreenSwitchCorners)) {
    options = m_config->getOptions("");
  }
  if (options != nullptr && options->contains(kOptionScreenSwitchCorners)) {
    // get corner mask and size
    Config::ScreenOptions::const_iterator i = options->find(kOptionScreenSwitchCorners);
    auto corners = static_cast<uint32_t>(i->second);
    i = options->find(kOptionScreenSwitchCornerSize);
    int32_t size = 0;
    if (i != options->end()) {
      size = i->second;
    }
    // see if we're in a locked corner
    if ((getCorner(m_active, xActive, yActive, size) & corners) != 0) {
      // yep, no switching
      LOG_VERBOSE("locked in corner");
      stopSwitch();
      return finish(SwitchPolicyCondition::LockedCorner);
    }
  }

  // ignore if mouse is locked to screen and don't try to switch later
  if (!preventSwitch && isLockedToScreen()) {
    LOG_VERBOSE("locked to screen");
    stopSwitch();
    return finish(SwitchPolicyCondition::LockedToScreen);
  }

  return finish(preventSwitch ? pendingConditions : SwitchPolicyCondition::None);
}

void Server::noSwitch(int32_t x, int32_t y)
{
  armSwitchTwoTap(x, y);
  stopSwitchWait();
}

void Server::stopSwitch()
{
  m_switchScreen = nullptr;
  m_switchDir = Direction::NoDirection;
  stopSwitchTwoTap();
  stopSwitchWait();
}

void Server::startSwitchTwoTap()
{
  m_switchTwoTapEngaged = true;
  m_switchTwoTapArmed = false;
  m_switchTwoTapTimer.reset();
  LOG_VERBOSE("waiting for second tap");
}

void Server::armSwitchTwoTap(int32_t x, int32_t y)
{
  if (m_switchTwoTapEngaged) {
    if (m_switchTwoTapTimer.getTime() > m_switchTwoTapDelay) {
      // second tap took too long.  disengage.
      stopSwitchTwoTap();
    } else if (!m_switchTwoTapArmed) {
      // still time for a double tap.  see if we left the tap
      // zone and, if so, arm the two tap.
      int32_t ax;
      int32_t ay;
      int32_t aw;
      int32_t ah;
      m_active->getShape(ax, ay, aw, ah);
      int32_t tapZone = m_primaryClient->getJumpZoneSize();
      if (tapZone < m_switchTwoTapZone) {
        tapZone = m_switchTwoTapZone;
      }
      if (x >= ax + tapZone && x < ax + aw - tapZone && y >= ay + tapZone && y < ay + ah - tapZone) {
        // win32 can generate bogus mouse events that appear to
        // move in the opposite direction that the mouse actually
        // moved.  try to ignore that crap here.
        switch (m_switchDir) {
          using enum Direction;
        case Left:
          m_switchTwoTapArmed = (m_xDelta > 0 && m_xDelta2 > 0);
          break;

        case Right:
          m_switchTwoTapArmed = (m_xDelta < 0 && m_xDelta2 < 0);
          break;

        case Top:
          m_switchTwoTapArmed = (m_yDelta > 0 && m_yDelta2 > 0);
          break;

        case Bottom:
          m_switchTwoTapArmed = (m_yDelta < 0 && m_yDelta2 < 0);
          break;

        default:
          break;
        }
      }
    }
  }
}

void Server::stopSwitchTwoTap()
{
  m_switchTwoTapEngaged = false;
  m_switchTwoTapArmed = false;
}

bool Server::isSwitchTwoTapStarted() const
{
  return m_switchTwoTapEngaged;
}

bool Server::shouldSwitchTwoTap() const
{
  // this is the second tap if two-tap is armed and this tap
  // came fast enough
  return (m_switchTwoTapArmed && m_switchTwoTapTimer.getTime() <= m_switchTwoTapDelay);
}

void Server::startSwitchWait(int32_t x, int32_t y)
{
  stopSwitchWait();
  m_switchWaitX = x;
  m_switchWaitY = y;
  m_switchWaitTimer = m_events->newOneShotTimer(m_switchWaitDelay, this);
  LOG_VERBOSE("waiting to switch");
}

void Server::stopSwitchWait()
{
  if (m_switchWaitTimer != nullptr) {
    m_events->deleteTimer(m_switchWaitTimer);
    m_switchWaitTimer = nullptr;
  }
}

bool Server::isSwitchWaitStarted() const
{
  return (m_switchWaitTimer != nullptr);
}

uint32_t Server::getCorner(const BaseClientProxy *client, int32_t x, int32_t y, int32_t size) const
{
  assert(client != nullptr);

  // get client screen shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  client->getShape(ax, ay, aw, ah);

  // check for x,y on the left or right
  int32_t xSide;
  if (x <= ax) {
    xSide = -1;
  } else if (x >= ax + aw - 1) {
    xSide = 1;
  } else {
    xSide = 0;
  }

  // check for x,y on the top or bottom
  int32_t ySide;
  if (y <= ay) {
    ySide = -1;
  } else if (y >= ay + ah - 1) {
    ySide = 1;
  } else {
    ySide = 0;
  }

  // if against the left or right then check if y is within size
  if (xSide != 0) {
    if (y < ay + size) {
      return (xSide < 0) ? s_topLeftCornerMask : s_topRightCornerMask;
    } else if (y >= ay + ah - size) {
      return (xSide < 0) ? s_bottomLeftCornerMask : s_bottomRightCornerMask;
    }
  }

  // if against the left or right then check if y is within size
  if (ySide != 0) {
    if (x < ax + size) {
      return (ySide < 0) ? s_topLeftCornerMask : s_bottomLeftCornerMask;
    } else if (x >= ax + aw - size) {
      return (ySide < 0) ? s_topRightCornerMask : s_bottomRightCornerMask;
    }
  }

  return s_noCornerMask;
}

void Server::stopRelativeMoves()
{
  if (m_relativeMoves && m_active != m_primaryClient) {
    // warp to the center of the active client so we know where we are
    int32_t ax;
    int32_t ay;
    int32_t aw;
    int32_t ah;
    m_active->getShape(ax, ay, aw, ah);
    m_x = ax + (aw >> 1);
    m_y = ay + (ah >> 1);
    m_xDelta = 0;
    m_yDelta = 0;
    m_xDelta2 = 0;
    m_yDelta2 = 0;
    LOG_VERBOSE("synchronize move on %s by %d,%d", getName(m_active).c_str(), m_x, m_y);
    m_active->mouseMove(m_x, m_y);
  }
}

void Server::sendOptions(BaseClientProxy *client) const
{
  OptionsList optionsList;

  // look up options for client
  const Config::ScreenOptions *options = m_config->getOptions(getName(client));
  if (options != nullptr) {
    // convert options to a more convenient form for sending
    optionsList.reserve(2 * options->size());
    for (auto [optionId, optionValue] : *options) {
      optionsList.push_back(optionId);
      optionsList.push_back(static_cast<uint32_t>(optionValue));
    }
  }

  // look up global options
  options = m_config->getOptions("");
  if (options != nullptr) {
    // convert options to a more convenient form for sending
    optionsList.reserve(optionsList.size() + 2 * options->size());
    for (auto [optionId, optionValue] : *options) {
      optionsList.push_back(optionId);
      optionsList.push_back(static_cast<uint32_t>(optionValue));
    }
  }

  // send the options
  client->resetOptions();
  client->setOptions(optionsList);
}

void Server::processOptions()
{
  const Config::ScreenOptions *options = m_config->getOptions("");
  if (options == nullptr) {
    return;
  }

  const bool wasLockedToScreen = isLockedToScreenServer();
  bool newRelativeMoves = m_relativeMoves;
  for (auto [optionId, optionValue] : *options) {
    const OptionID id = optionId;
    const OptionValue value = optionValue;
    if (id == kOptionScreenSwitchDelay) {
      m_switchWaitDelay = 1.0e-3 * static_cast<double>(value);
      if (m_switchWaitDelay < 0.0) {
        m_switchWaitDelay = 0.0;
      }
      stopSwitchWait();
    } else if (id == kOptionScreenSwitchTwoTap) {
      m_switchTwoTapDelay = 1.0e-3 * static_cast<double>(value);
      if (m_switchTwoTapDelay < 0.0) {
        m_switchTwoTapDelay = 0.0;
      }
      stopSwitchTwoTap();
    } else if (id == kOptionRelativeMouseMoves) {
      newRelativeMoves = (value != 0);
    } else if (id == kOptionDefaultLockToScreenState) {
      m_defaultLockToScreenState = (value != 0);
    } else if (id == kOptionDisableLockToScreen) {
      m_disableLockToScreen = (value != 0);
    } else if (id == kOptionClipboardSharing) {
      m_enableClipboard = value;
      if (!m_enableClipboard) {
        LOG_INFO("clipboard sharing is disabled");
      }
    } else if (id == kOptionClipboardSharingSize) {
      if (value <= 0) {
        m_maximumClipboardSize = 0;
        LOG_INFO(
            "clipboard sharing is disabled because the "
            "maximum shared clipboard size is set to 0"
        );
      } else {
        m_maximumClipboardSize = static_cast<size_t>(value);
      }
    }
  }

  if (m_disableLockToScreen && m_lockedToScreen) {
    m_lockedToScreen = false;
    LOG_INFO("cursor unlocked because lock to screen is disabled");
  }

  if ((m_relativeMoves && !newRelativeMoves) || (wasLockedToScreen && !isLockedToScreenServer())) {
    stopRelativeMoves();
  }
  m_relativeMoves = newRelativeMoves;
  m_protocol = Settings::networkProtocol();
}

void Server::handleShapeChanged(BaseClientProxy *client)
{
  if (!m_clientSet.contains(client)) {
    return;
  }

  LOG_DEBUG("screen \"%s\" shape changed", getName(client).c_str());
  syncClientCursorPosition(client, "shape update");

  // handle resolution change to primary screen
  if (client == m_primaryClient) {
    if (client == m_active) {
      onMouseMovePrimary(m_x, m_y);
    } else {
      onMouseMoveSecondary(0, 0);
    }
  }
}

void Server::handleInfoChanged(BaseClientProxy *client)
{
  if (!m_clientSet.contains(client)) {
    return;
  }

  syncClientCursorPosition(client, "info update");
}

void Server::syncClientCursorPosition(BaseClientProxy *client, const char *reason)
{
  if (reason == nullptr) {
    reason = "unknown";
  }

  int32_t x;
  int32_t y;
  client->getCursorPos(x, y);
  client->setJumpCursorPos(x, y);

  if (client == m_active) {
    if (m_x != x || m_y != y) {
      LOG_DEBUG(
          "active screen \"%s\" cursor synced from %s: old=%d,%d new=%d,%d", getName(client).c_str(), reason, m_x, m_y,
          x, y
      );
      m_xDelta = 0;
      m_yDelta = 0;
      m_xDelta2 = 0;
      m_yDelta2 = 0;
    }
    // A client cursor report starts a new motion epoch. Pending edge intent
    // belongs to the old coordinate stream and must not survive the resync.
    stopSwitch();
    m_switchBackGuard.resynchronize(x, y);
    clearNoNeighborEdgeGuard();
    m_x = x;
    m_y = y;
  }
}

void Server::handleClipboardGrabbed(const Event &event, BaseClientProxy *grabber)
{
  if (!m_enableClipboard || (m_maximumClipboardSize == 0)) {
    return;
  }

  // ignore events from unknown clients
  if (!m_clientSet.contains(grabber)) {
    return;
  }
  const auto *info = static_cast<const IScreen::ClipboardInfo *>(event.getData());
  if (info == nullptr) {
    LOG_WARN("ignored clipboard grab from screen \"%s\" because event data is missing", getName(grabber).c_str());
    return;
  }
  if (info->m_id >= kClipboardEnd) {
    LOG_WARN(
        "ignored clipboard grab from screen \"%s\" because clipboard id %d is invalid", getName(grabber).c_str(),
        info->m_id
    );
    return;
  }
  if (grabber->usesAtomicClipboardPublish()) {
    LOG_DEBUG(
        "ignored advisory clipboard grab from atomic publisher \"%s\" for clipboard %d", getName(grabber).c_str(),
        info->m_id
    );
    return;
  }
  const auto grabberName = getName(grabber);
  if (!m_clipboardPublicationAuthority.isFocusValid(grabberName, info->m_sequenceNumber)) {
    LOG_DEBUG(
        "ignored clipboard grab from screen \"%s\" for clipboard %d with invalid focus sequence %u",
        grabberName.c_str(), info->m_id, info->m_sequenceNumber
    );
    return;
  }

  ClipboardInfo &clipboard = m_clipboards[info->m_id];
  if (grabber != m_primaryClient && isClipboardSequenceOlder(info->m_sequenceNumber, clipboard.m_sourceSequence)) {
    LOG_DEBUG("ignored screen \"%s\" grab of clipboard %d", getName(grabber).c_str(), info->m_id);
    return;
  }

  // mark screen as owning clipboard
  const auto revision = nextClipboardRevision();
  LOG_INFO(
      "screen \"%s\" grabbed clipboard %d from \"%s\", revision=%u", grabberName.c_str(), info->m_id,
      clipboard.m_clipboardOwner.c_str(), revision
  );
  clipboard.m_clipboardOwner = getName(grabber);
  clipboard.m_sourceSequence = info->m_sequenceNumber;
  clipboard.m_revision = revision;

  // The clipboard payload is not known yet. Keep the last confirmed payload
  // in place until the owner sends data, so failed or oversized transfers do
  // not clear other screens.
  for (const auto &entry : m_clients) {
    auto *client = entry.second;
    const auto preserveIncomingSequence =
        client == grabber ? std::optional<uint32_t>{info->m_sequenceNumber} : std::nullopt;
    client->supersedeClipboardTransfers(info->m_id, preserveIncomingSequence);
    client->setClipboardDirty(info->m_id, client != grabber);
  }

  if (grabber == m_primaryClient) {
    onClipboardChanged(m_primaryClient, info->m_id, info->m_sequenceNumber);
  }
}

void Server::handleClipboardChanged(const Event &event, BaseClientProxy *client)
{
  // ignore events from unknown clients
  if (!m_clientSet.contains(client)) {
    return;
  }
  const auto *info = static_cast<const IScreen::ClipboardInfo *>(event.getData());
  if (info == nullptr) {
    LOG_WARN("ignored clipboard update from screen \"%s\" because event data is missing", getName(client).c_str());
    return;
  }
  if (info->m_id >= kClipboardEnd) {
    LOG_WARN(
        "ignored clipboard update from screen \"%s\" because clipboard id %d is invalid", getName(client).c_str(),
        info->m_id
    );
    return;
  }
  if (client->usesAtomicClipboardPublish()) {
    if (!client->hasPendingClipboardPublish(info->m_id, info->m_sequenceNumber)) {
      LOG_DEBUG("ignored clipboard publication event from \"%s\" without a pending transfer", getName(client).c_str());
      return;
    }
    onAtomicClipboardPublished(client, info->m_id, info->m_sequenceNumber);
    return;
  }
  onClipboardChanged(client, info->m_id, info->m_sequenceNumber);
}

void Server::handleKeyDownEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::KeyInfo *>(event.getData());
  auto lang = AppUtil::instance().getCurrentLanguageCode();
  onKeyDown(info->m_key, info->m_mask, info->m_button, lang, info->m_screens.c_str());
}

void Server::handleKeyUpEvent(const Event &event)
{
  auto *info = static_cast<IPlatformScreen::KeyInfo *>(event.getData());
  onKeyUp(info->m_key, info->m_mask, info->m_button, info->m_screens.c_str());
}

void Server::handleKeyRepeatEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::KeyInfo *>(event.getData());
  auto lang = AppUtil::instance().getCurrentLanguageCode();
  onKeyRepeat(info->m_key, info->m_mask, info->m_count, info->m_button, lang);
}

void Server::handleButtonDownEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::ButtonInfo *>(event.getData());
  onMouseDown(info->m_button);
}

void Server::handleButtonUpEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::ButtonInfo *>(event.getData());
  onMouseUp(info->m_button);
}

void Server::handleMotionPrimaryEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::MotionInfo *>(event.getData());
  onMouseMovePrimary(info->m_x, info->m_y);
}

void Server::handleMotionSecondaryEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::MotionInfo *>(event.getData());
  onMouseMoveSecondary(info->m_x, info->m_y);
}

void Server::handleWheelEvent(const Event &event)
{
  const auto *info = static_cast<IPlatformScreen::WheelInfo *>(event.getData());
  onMouseWheel(info->m_xDelta, info->m_yDelta);
}

void Server::handleSwitchWaitTimeout()
{
  if (m_switchScreen == nullptr) {
    stopSwitch();
    return;
  }

  // ignore if mouse is locked to screen
  if (isLockedToScreen()) {
    LOG_VERBOSE("locked to screen");
    stopSwitch();
    return;
  }

  BaseClientProxy *newScreen = m_switchScreen;
  BaseClientProxy *previousScreen = m_active;
  const Direction switchDirection = m_switchDir;
  const auto transitionStartedAt = SwitchBackGuard::Clock::now();
  switchScreen(newScreen, m_switchWaitX, m_switchWaitY, false, "switch-wait-timeout");
  if (m_active == newScreen && previousScreen != newScreen) {
    startSwitchBackGuard(newScreen, previousScreen, oppositeDirection(switchDirection), transitionStartedAt);
  }
}

void Server::handleClientDisconnected(BaseClientProxy *client)
{
  // client has disconnected.  it might be an old client or an
  // active client.  we don't care so just handle it both ways.
  removeActiveClient(client);
  removeOldClient(client);

  // m_clients always contains the primary (server) screen, so 1 means no remote clients.
  using enum deskflow::core::ConnectionState;
  ipcSendConnectionState(m_clients.size() <= 1 ? Listening : Connected);
  sendConnectedClientsIpc();

  delete client;
}

void Server::handleClientCloseTimeout(BaseClientProxy *client)
{
  // client took too long to disconnect.  just dump it.
  LOG_INFO("forced disconnection of client \"%s\"", getName(client).c_str());
  removeOldClient(client);

  delete client;
}

void Server::handleSwitchToScreenEvent(const Event &event)
{
  const auto *info = static_cast<SwitchToScreenInfo *>(event.getData());

  ClientList::const_iterator index = m_clients.find(info->m_screen);
  if (index == m_clients.end()) {
    LOG_VERBOSE("screen \"%s\" not active", info->m_screen.c_str());
  } else {
    jumpToScreen(index->second, "switch-to-screen-action");
  }
}

void Server::handleSwitchInDirectionEvent(const Event &event)
{
  const auto *info = static_cast<SwitchInDirectionInfo *>(event.getData());

  // jump to screen in chosen direction from center of this screen
  const auto result = getNeighbor(m_active, info->m_direction, {m_x, m_y});
  if (!result.isMapped()) {
    LOG_VERBOSE(
        "direction action neighbor lookup missed: source=\"%s\" direction=%s reason=%s", getName(m_active).c_str(),
        Config::dirName(info->m_direction), neighborMapStatusKeyword(result.status()).data()
    );
  } else {
    jumpToScreen(result.target(), "switch-in-direction-action");
  }
}

void Server::handleToggleScreenEvent(const Event &)
{
  // Get the list of connected screens in config order
  std::vector<std::string> screens;
  getClients(screens);

  if (screens.size() < 2) {
    LOG_ERR("not enough screens to toggle");
    return;
  }

  // Find the current active screen
  std::string currentScreen = getName(m_active);
  auto it = std::ranges::find(screens, currentScreen);
  if (it == screens.end()) {
    LOG_ERR("current screen not found in list");
    return;
  }

  // Find the next screen
  auto nextIt = it + 1;
  if (nextIt == screens.end()) {
    nextIt = screens.begin();
  }

  // Find the client for the next screen
  ClientList::const_iterator clientIt = m_clients.find(*nextIt);
  if (clientIt == m_clients.end()) {
    LOG_ERR("next screen not active");
    return;
  }

  jumpToScreen(clientIt->second, "toggle-screen-action");
}

void Server::handleKeyboardBroadcastEvent(const Event &event)
{
  const auto *info = static_cast<KeyboardBroadcastInfo *>(event.getData());

  // choose new state
  bool newState;
  switch (info->m_state) {
  default:
  case KeyboardBroadcastInfo::kOn:
    newState = true;
    break;

  case KeyboardBroadcastInfo::kOff:
    newState = false;
    break;

  case KeyboardBroadcastInfo::kToggle:
    newState = !m_keyboardBroadcasting;
    break;
  }

  // enter new state
  if (newState != m_keyboardBroadcasting || info->m_screens != m_keyboardBroadcastingScreens) {
    m_keyboardBroadcasting = newState;
    m_keyboardBroadcastingScreens = info->m_screens;
    LOG(
        (CLOG_DEBUG "keyboard broadcasting %s: %s", m_keyboardBroadcasting ? "on" : "off",
         m_keyboardBroadcastingScreens.c_str())
    );
  }
}

void Server::handleLockCursorToScreenEvent(const Event &event)
{
  const auto *info = static_cast<LockCursorToScreenInfo *>(event.getData());

  if (m_disableLockToScreen) {
    if (m_lockedToScreen) {
      m_lockedToScreen = false;
      stopRelativeMoves();
      m_primaryClient->reconfigure(getActivePrimarySides());
    }
    LOG_DEBUG("ignored lock to screen action because the feature is disabled");
    return;
  }

  // choose new state
  bool newState;
  switch (info->m_state) {
  default:
  case LockCursorToScreenInfo::kOn:
    newState = true;
    break;

  case LockCursorToScreenInfo::kOff:
    newState = false;
    break;

  case LockCursorToScreenInfo::kToggle:
    newState = !m_lockedToScreen;
    break;
  }

  // enter new state
  if (newState != m_lockedToScreen) {
    m_lockedToScreen = newState;
    LOG_INFO("cursor %s current screen", m_lockedToScreen ? "locked to" : "unlocked from");

    m_primaryClient->reconfigure(getActivePrimarySides());
    if (!isLockedToScreenServer()) {
      stopRelativeMoves();
    }
  }
}

void Server::onClipboardChanged(const BaseClientProxy *sender, ClipboardID id, uint32_t seqNum)
{
  ClipboardInfo &clipboard = m_clipboards[id];

  if (seqNum != clipboard.m_sourceSequence) {
    LOG_INFO(
        "ignored screen \"%s\" update of clipboard %d because sequence %u was superseded by %u",
        getName(sender).c_str(), id, seqNum, clipboard.m_sourceSequence
    );
    return;
  }

  const auto owner = m_clients.find(clipboard.m_clipboardOwner);
  if (owner == m_clients.end()) {
    LOG_INFO(
        "ignored screen \"%s\" update of clipboard %d because owner \"%s\" is not connected", getName(sender).c_str(),
        id, clipboard.m_clipboardOwner.c_str()
    );
    return;
  }
  if (sender != owner->second) {
    LOG_INFO(
        "ignored screen \"%s\" update of clipboard %d because owner is \"%s\"", getName(sender).c_str(), id,
        clipboard.m_clipboardOwner.c_str()
    );
    return;
  }

  const bool prepareClipboardReceivers = sender == m_primaryClient;
  if (prepareClipboardReceivers) {
    for (const auto &entry : m_clients) {
      auto *client = entry.second;
      if (client != sender) {
        client->beginClipboardSend();
      }
    }
  }
  auto finishClipboardReceivers = deskflow::finally([this, sender, prepareClipboardReceivers]() {
    if (!prepareClipboardReceivers) {
      return;
    }
    for (const auto &entry : m_clients) {
      auto *client = entry.second;
      if (client != sender) {
        client->finishClipboardSend();
      }
    }
  });

  Clipboard candidate;
  if (!sender->getClipboard(id, &candidate)) {
    LOG_INFO(
        "ignored screen \"%s\" update of clipboard %d because clipboard data is unavailable", getName(sender).c_str(),
        id
    );
    return;
  }
  std::string data = candidate.marshall();
  if (data.size() <= sizeof(uint32_t)) {
    LOG_INFO(
        "ignored screen \"%s\" update of clipboard %d because it has no supported formats", getName(sender).c_str(), id
    );
    return;
  }
  if (data.size() > m_maximumClipboardSize * 1024) {
    LOG_WARN("not sending clipboard data, exceeds limit: %i KB", m_maximumClipboardSize);
    return;
  }

  if (data != clipboard.m_clipboardData && clipboard.m_revision == clipboard.m_committedRevision) {
    clipboard.m_revision = nextClipboardRevision();
  }

  // ignore if data hasn't changed
  if (data == clipboard.m_clipboardData) {
    const bool revisionChanged = clipboard.m_revision != clipboard.m_committedRevision;
    clipboard.m_committedRevision = clipboard.m_revision;
    LOG_DEBUG("ignored screen \"%s\" update of clipboard %d (unchanged)", clipboard.m_clipboardOwner.c_str(), id);
    if (revisionChanged) {
      broadcastClipboard(id, sender);
    }
    return;
  }

  // got new data
  LOG_INFO(
      "screen \"%s\" updated clipboard %d, revision=%u", clipboard.m_clipboardOwner.c_str(), id, clipboard.m_revision
  );
  Clipboard::copy(&clipboard.m_clipboard, &candidate);
  clipboard.m_clipboardData = data;
  clipboard.m_committedRevision = clipboard.m_revision;

  broadcastClipboard(id, sender);
}

void Server::onAtomicClipboardPublished(BaseClientProxy *sender, ClipboardID id, uint32_t sequence)
{
  const auto senderName = getName(sender);
  Clipboard candidate;
  if (!sender->getClipboard(id, &candidate)) {
    LOG_INFO(
        "rejected atomic clipboard publication from \"%s\" for clipboard %d because data is unavailable",
        senderName.c_str(), id
    );
    sender->completeClipboardPublish(id, sequence, ClipboardTransferCancelReason::Invalid);
    return;
  }

  const auto data = candidate.marshall();
  if (data.size() <= sizeof(uint32_t)) {
    LOG_INFO(
        "rejected atomic clipboard publication from \"%s\" for clipboard %d because it has no supported formats",
        senderName.c_str(), id
    );
    sender->completeClipboardPublish(id, sequence, ClipboardTransferCancelReason::Invalid);
    return;
  }

  const auto maximumBytes = m_maximumClipboardSize * 1024;
  if (data.size() > maximumBytes) {
    LOG_WARN(
        "rejected atomic clipboard publication from \"%s\" for clipboard %d because size=%zu exceeds limit=%zu",
        senderName.c_str(), id, data.size(), maximumBytes
    );
    sender->completeClipboardPublish(id, sequence, ClipboardTransferCancelReason::Invalid);
    return;
  }

  auto &clipboard = m_clipboards[id];
  const auto decision = m_clipboardPublicationAuthority.evaluate(
      senderName, sequence, clipboard.m_clipboardOwner, clipboard.m_sourceSequence, clipboard.m_clipboardData, data
  );
  if (decision == deskflow::server::ClipboardPublicationAuthority::Decision::InvalidFocus) {
    LOG_INFO(
        "rejected atomic clipboard publication from \"%s\" for clipboard %d with invalid focus sequence %u",
        senderName.c_str(), id, sequence
    );
    sender->completeClipboardPublish(id, sequence, ClipboardTransferCancelReason::Superseded);
    return;
  }
  if (decision == deskflow::server::ClipboardPublicationAuthority::Decision::Superseded) {
    LOG_INFO(
        "rejected superseded atomic clipboard publication from \"%s\" for clipboard %d sequence=%u current=%u",
        senderName.c_str(), id, sequence, clipboard.m_sourceSequence
    );
    sender->completeClipboardPublish(id, sequence, ClipboardTransferCancelReason::Superseded);
    return;
  }
  if (decision == deskflow::server::ClipboardPublicationAuthority::Decision::Duplicate) {
    LOG_DEBUG(
        "accepted duplicate atomic clipboard publication from \"%s\" for clipboard %d sequence=%u", senderName.c_str(),
        id, sequence
    );
    sender->completeClipboardPublish(id, sequence, std::nullopt);
    return;
  }

  const auto revision = nextClipboardRevision();
  clipboard.m_clipboardOwner = senderName;
  clipboard.m_sourceSequence = sequence;
  clipboard.m_revision = revision;
  Clipboard::copy(&clipboard.m_clipboard, &candidate);
  clipboard.m_clipboardData = data;
  clipboard.m_committedRevision = revision;

  LOG_INFO(
      "atomically committed clipboard %d from \"%s\", focus=%u revision=%u", id, senderName.c_str(), sequence, revision
  );
  broadcastClipboard(id, sender);
  sender->completeClipboardPublish(id, sequence, std::nullopt);
}

void Server::broadcastClipboard(ClipboardID id, const BaseClientProxy *sender)
{
  const auto &clipboard = m_clipboards[id];
  for (const auto &entry : m_clients) {
    auto *client = entry.second;
    const bool shouldReceive = client != sender;
    client->setClipboardDirty(id, shouldReceive);
    if (shouldReceive) {
      client->setClipboard(id, &clipboard.m_clipboard, clipboard.m_committedRevision);
    }
  }
}

uint32_t Server::nextClipboardRevision()
{
  const auto revision = m_nextClipboardRevision++;
  if (m_nextClipboardRevision == 0) {
    m_nextClipboardRevision = 1;
  }
  return revision;
}

void Server::onScreensaver(bool activated)
{
  LOG_DEBUG("onScreenSaver %s", activated ? "activated" : "deactivated");

  if (activated && m_active != m_primaryClient) {
    LOG_INFO(
        "primary screen saver activated while \"%s\" is active; keeping remote screen active", getName(m_active).c_str()
    );
    m_activeSaver = nullptr;
    return;
  }

  if (activated) {
    // save current screen and position
    m_activeSaver = m_active;
    m_xSaver = m_x;
    m_ySaver = m_y;
  } else {
    // jump back to previous screen and position.  we must check
    // that the position is still valid since the screen may have
    // changed resolutions while the screen saver was running.
    if (m_activeSaver != nullptr && m_activeSaver != m_primaryClient) {
      // check position
      BaseClientProxy *screen = m_activeSaver;
      int32_t x;
      int32_t y;
      int32_t w;
      int32_t h;
      screen->getShape(x, y, w, h);
      int32_t zoneSize = getJumpZoneSize(screen);
      if (m_xSaver < x + zoneSize) {
        m_xSaver = x + zoneSize;
      } else if (m_xSaver >= x + w - zoneSize) {
        m_xSaver = x + w - zoneSize - 1;
      }
      if (m_ySaver < y + zoneSize) {
        m_ySaver = y + zoneSize;
      } else if (m_ySaver >= y + h - zoneSize) {
        m_ySaver = y + h - zoneSize - 1;
      }

      // jump
      switchScreen(screen, m_xSaver, m_ySaver, false, "screensaver-deactivated-restore");
    }

    // reset state
    m_activeSaver = nullptr;
  }

  // send message to all clients
  for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
    BaseClientProxy *client = index->second;
    client->screensaver(activated);
  }
}

void Server::onKeyDown(KeyID id, KeyModifierMask mask, KeyButton button, const std::string &lang, const char *screens)
{
  LOG_VERBOSE("onKeyDown id=%d mask=0x%04x button=0x%04x lang=%s", id, mask, button, lang.c_str());
  assert(m_active != nullptr);

  // relay
  if (!m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(screens)) {
    m_active->keyDown(id, mask, button, lang);
  } else {
    if (!screens && m_keyboardBroadcasting) {
      screens = m_keyboardBroadcastingScreens.c_str();
      if (IKeyState::KeyInfo::isDefault(screens)) {
        screens = "*";
      }
    }
    for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
      if (IKeyState::KeyInfo::contains(screens, index->first)) {
        index->second->keyDown(id, mask, button, lang);
      }
    }
  }
}

void Server::onKeyUp(KeyID id, KeyModifierMask mask, KeyButton button, const char *screens)
{
  LOG_VERBOSE("onKeyUp id=%d mask=0x%04x button=0x%04x", id, mask, button);
  assert(m_active != nullptr);

  // relay
  if (!m_keyboardBroadcasting && IKeyState::KeyInfo::isDefault(screens)) {
    m_active->keyUp(id, mask, button);
  } else {
    if (!screens && m_keyboardBroadcasting) {
      screens = m_keyboardBroadcastingScreens.c_str();
      if (IKeyState::KeyInfo::isDefault(screens)) {
        screens = "*";
      }
    }
    for (ClientList::const_iterator index = m_clients.begin(); index != m_clients.end(); ++index) {
      if (IKeyState::KeyInfo::contains(screens, index->first)) {
        index->second->keyUp(id, mask, button);
      }
    }
  }
}

void Server::onKeyRepeat(KeyID id, KeyModifierMask mask, int32_t count, KeyButton button, const std::string &lang)
{
  LOG(
      (CLOG_VERBOSE "onKeyRepeat id=%d mask=0x%04x count=%d button=0x%04x lang=\"%s\"", id, mask, count, button,
       lang.c_str())
  );
  assert(m_active != nullptr);

  // relay
  m_active->keyRepeat(id, mask, count, button, lang);
}

void Server::onMouseDown(ButtonID id)
{
  LOG_VERBOSE("onMouseDown id=%d", id);
  assert(m_active != nullptr);

  // relay
  m_active->mouseDown(id);
}

void Server::onMouseUp(ButtonID id)
{
  LOG_VERBOSE("onMouseUp id=%d", id);
  assert(m_active != nullptr);

  // relay
  m_active->mouseUp(id);
}

bool Server::onMouseMovePrimary(int32_t x, int32_t y)
{
  const bool logMouseMotion = shouldLogMouseMotion();
  if (logMouseMotion) {
    LOG_VERBOSE("mouse position on primary: %d,%d", x, y);
  }

  // mouse move on primary (server's) screen
  if (m_active != m_primaryClient) {
    // stale event -- we're actually on a secondary screen
    return false;
  }

  // save last delta
  m_xDelta2 = m_xDelta;
  m_yDelta2 = m_yDelta;

  // save current delta
  m_xDelta = x - m_x;
  m_yDelta = y - m_y;

  // save position
  m_x = x;
  m_y = y;

  // get screen shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  m_active->getShape(ax, ay, aw, ah);
  int32_t zoneSize = getJumpZoneSize(m_active);
  updateSwitchBackGuard(ax, ay, aw, ah, m_x, m_y);
  updateNoNeighborEdgeGuard(ax, ay, aw, ah, m_x, m_y);

  // clamp position to screen
  int32_t xc = x;
  int32_t yc = y;
  if (xc < ax + zoneSize) {
    xc = ax;
  } else if (xc >= ax + aw - zoneSize) {
    xc = ax + aw - 1;
  }
  if (yc < ay + zoneSize) {
    yc = ay;
  } else if (yc >= ay + ah - zoneSize) {
    yc = ay + ah - 1;
  }

  // see if we should change screens
  // when the cursor is in a corner, there may be a screen either
  // horizontally or vertically.  check both directions.
  using enum Direction;
  auto dirh = NoDirection;
  auto dirv = NoDirection;
  int32_t xh = x;
  int32_t yv = y;
  if (x < ax + zoneSize) {
    xh -= zoneSize;
    dirh = Left;
  } else if (x >= ax + aw - zoneSize) {
    xh += zoneSize;
    dirh = Right;
  }
  if (y < ay + zoneSize) {
    yv -= zoneSize;
    dirv = Top;
  } else if (y >= ay + ah - zoneSize) {
    yv += zoneSize;
    dirv = Bottom;
  }
  if (dirh == NoDirection && dirv == NoDirection) {
    // still on local screen
    noSwitch(x, y);
    return false;
  }

  // check both horizontally and vertically
  std::array<Direction, 2> dirs = {dirh, dirv};
  std::array<int32_t, 2> xs = {xh, x};
  std::array<int32_t, 2> ys = {y, yv};
  for (int i = 0; i < 2; ++i) {
    Direction dir = dirs.at(i);
    if (dir == NoDirection) {
      continue;
    }
    if (isNoNeighborEdgeGuardBlocked(m_active, dir)) {
      continue;
    }

    // get jump destination
    const auto mapping = mapToNeighbor(m_active, dir, {xs.at(i), ys.at(i)});
    if (!mapping.isMapped()) {
      recordNeighborMiss(dir, mapping);
      stopSwitch();
      continue;
    }

    // should we switch or not?
    const auto policy = applySwitchPolicy(mapping.target(), dir, mapping.position().x, mapping.position().y, xc, yc);
    if (policy.shouldSwitch()) {
      LOG_INFO(
          "primary edge switch from \"%s\" to \"%s\" on %s: cursor=%d,%d clamped=%d,%d target=%d,%d bounds=%d,%d "
          "%dx%d zone=%d delta=%+d,%+d",
          getName(m_active).c_str(), getName(mapping.target()).c_str(), Config::dirName(dir), m_x, m_y, xc, yc,
          mapping.position().x, mapping.position().y, ax, ay, aw, ah, zoneSize, m_xDelta, m_yDelta
      );

      // switch screen
      BaseClientProxy *previousScreen = m_active;
      const auto transitionStartedAt = SwitchBackGuard::Clock::now();
      switchScreen(mapping.target(), mapping.position().x, mapping.position().y, false, "primary-edge-motion");
      if (m_active == mapping.target() && previousScreen != mapping.target()) {
        startSwitchBackGuard(mapping.target(), previousScreen, oppositeDirection(dir), transitionStartedAt);
      }
      return true;
    }
  }

  return false;
}

void Server::onMouseMoveSecondary(int32_t dx, int32_t dy)
{
  const bool logMouseMotion = shouldLogMouseMotion();
  if (logMouseMotion) {
    LOG_VERBOSE("mouse delta on secondary: %+d,%+d", dx, dy);
  }

  // TODO: move this to client side and use a qt setting or cli arg instead of env var.
  const static auto adjustEnv = "DESKFLOW_MOUSE_ADJUSTMENT";
  if (const char *envVal = std::getenv(adjustEnv); envVal) {
    try {
      double multiplier = std::stod(envVal);
      dx = static_cast<int32_t>(std::round(dx * multiplier));
      dy = static_cast<int32_t>(std::round(dy * multiplier));
      LOG_VERBOSE("adjusted mouse x %.2f: %+d,%+d", multiplier, dx, dy);
    } catch (const std::exception &e) {
      LOG_ERR("invalid %s value: %s", adjustEnv, e.what());
    }
  }

  // mouse move on secondary (client's) screen
  assert(m_active != nullptr);
  if (m_active == m_primaryClient) {
    // stale event -- we're actually on the primary screen
    return;
  }

  // if doing relative motion on secondary screens and we're locked
  // to the screen (which activates relative moves) then send a
  // relative mouse motion.  when we're doing this we pretend as if
  // the mouse isn't actually moving because we're expecting some
  // program on the secondary screen to warp the mouse on us, so we
  // have no idea where it really is.
  if (m_relativeMoves && isLockedToScreenServer()) {
    m_active->mouseRelativeMove(dx, dy);
    return;
  }

  // save old position
  const int32_t xOld = m_x;
  const int32_t yOld = m_y;

  // save last delta
  m_xDelta2 = m_xDelta;
  m_yDelta2 = m_yDelta;

  // save current delta
  m_xDelta = dx;
  m_yDelta = dy;

  // accumulate motion
  m_x += dx;
  m_y += dy;

  // get screen shape
  int32_t ax;
  int32_t ay;
  int32_t aw;
  int32_t ah;
  m_active->getShape(ax, ay, aw, ah);
  updateSwitchBackGuard(ax, ay, aw, ah, m_x, m_y);
  updateNoNeighborEdgeGuard(ax, ay, aw, ah, m_x, m_y);

  // Find horizontal and vertical candidates independently so a corner miss on
  // one axis cannot hide a valid neighbor on the other.
  bool jump = false;
  BaseClientProxy *newScreen = m_active;
  Direction switchDir = Direction::NoDirection;
  EdgeSwitchPosition switchPosition{m_x, m_y};
  const EdgeSwitchBounds switchBounds{ax, ay, aw, ah};
  const EdgeSwitchPosition requestedPosition{m_x, m_y};
  const auto switchDirections = makeEdgeSwitchDirections(switchBounds, requestedPosition);
  bool foundPolicyTarget = false;

  // Clamp the active-screen coordinate used by wait and double-tap options.
  const int32_t xc = std::clamp(m_x, ax, ax + aw - 1);
  const int32_t yc = std::clamp(m_y, ay, ay + ah - 1);

  if (switchDirections[0] == Direction::NoDirection && switchDirections[1] == Direction::NoDirection) {
    // If waiting and the mouse has left the pending border, cancel the wait
    // and arm the double tap.
    if (m_switchScreen != nullptr) {
      bool clearWait;
      const int32_t zoneSize = m_primaryClient->getJumpZoneSize();
      switch (m_switchDir) {
        using enum Direction;
      case Left:
        clearWait = (m_x >= ax + zoneSize);
        break;
      case Right:
        clearWait = (m_x <= ax + aw - 1 - zoneSize);
        break;
      case Top:
        clearWait = (m_y >= ay + zoneSize);
        break;
      case Bottom:
        clearWait = (m_y <= ay + ah - 1 - zoneSize);
        break;
      case NoDirection:
        clearWait = false;
        break;
      }
      if (clearWait) {
        noSwitch(m_x, m_y);
      }
    }
  } else {
    for (const Direction dir : switchDirections) {
      if (dir == Direction::NoDirection) {
        continue;
      }

      if (isNoNeighborEdgeGuardBlocked(m_active, dir)) {
        continue;
      }

      EdgeSwitchPosition candidatePosition = makeEdgeSwitchProbe(switchBounds, dir, requestedPosition);
      const auto mapping = mapToNeighbor(m_active, dir, candidatePosition);
      if (!mapping.isMapped()) {
        recordNeighborMiss(dir, mapping);
        continue;
      }

      // A valid target owns switch-wait, double-tap, and switch-back policy.
      // Do not let a second-axis lookup overwrite that state.
      foundPolicyTarget = true;
      const auto policy = applySwitchPolicy(mapping.target(), dir, mapping.position().x, mapping.position().y, xc, yc);
      if (!policy.shouldSwitch()) {
        break;
      }

      jump = true;
      newScreen = mapping.target();
      switchDir = dir;
      switchPosition = mapping.position();
      break;
    }

    if (!jump && !foundPolicyTarget) {
      stopSwitch();
    }
  }

  if (jump) {
    const int32_t newX = switchPosition.x;
    const int32_t newY = switchPosition.y;
    LOG_INFO(
        "secondary edge switch from \"%s\" to \"%s\" on %s: old=%d,%d delta=%+d,%+d target=%d,%d bounds=%d,%d %dx%d",
        getName(m_active).c_str(), getName(newScreen).c_str(), Config::dirName(switchDir), xOld, yOld, dx, dy, newX,
        newY, ax, ay, aw, ah
    );

    // switch screens
    BaseClientProxy *previousScreen = m_active;
    const auto transitionStartedAt = SwitchBackGuard::Clock::now();
    switchScreen(newScreen, newX, newY, false, "secondary-edge-motion");
    if (m_active == newScreen && previousScreen != newScreen) {
      startSwitchBackGuard(newScreen, previousScreen, oppositeDirection(switchDir), transitionStartedAt);
    }
  } else {
    // same screen.  clamp mouse to edge.
    const int32_t requestedX = xOld + dx;
    const int32_t requestedY = yOld + dy;
    m_x = requestedX;
    m_y = requestedY;
    bool clamped = false;
    if (m_x < ax) {
      m_x = ax;
      clamped = true;
    } else if (m_x > ax + aw - 1) {
      m_x = ax + aw - 1;
      clamped = true;
    }
    if (m_y < ay) {
      m_y = ay;
      clamped = true;
    } else if (m_y > ay + ah - 1) {
      m_y = ay + ah - 1;
      clamped = true;
    }

    if (clamped && logMouseMotion) {
      LOG_VERBOSE(
          "clamped secondary motion on \"%s\": old=%d,%d delta=%+d,%+d requested=%d,%d clamped=%d,%d bounds=%d,%d "
          "%dx%d",
          getName(m_active).c_str(), xOld, yOld, dx, dy, requestedX, requestedY, m_x, m_y, ax, ay, aw, ah
      );
    }

    // warp cursor if it moved.
    if (m_x != xOld || m_y != yOld) {
      m_active->mouseMove(m_x, m_y);
    }
  }
}

void Server::onMouseWheel(int32_t xDelta, int32_t yDelta)
{
  LOG_VERBOSE("onMouseWheel %+d,%+d", xDelta, yDelta);
  assert(m_active != nullptr);

  // relay
  m_active->mouseWheel(xDelta, yDelta);
}

bool Server::addClient(BaseClientProxy *client)
{
  std::string name = getName(client);
  if (m_clients.contains(name)) {
    return false;
  }

  // add event handlers
  m_events->addHandler(EventTypes::ScreenShapeChanged, client->getEventTarget(), [this, client](const auto &) {
    handleShapeChanged(client);
  });
  m_events->addHandler(EventTypes::ScreenInfoChanged, client->getEventTarget(), [this, client](const auto &) {
    handleInfoChanged(client);
  });
  m_events->addHandler(EventTypes::ClipboardGrabbed, client->getEventTarget(), [this, client](const auto &e) {
    handleClipboardGrabbed(e, client);
  });
  m_events->addHandler(EventTypes::ClipboardChanged, client->getEventTarget(), [this, client](const auto &e) {
    handleClipboardChanged(e, client);
  });

  // add to list
  m_clientSet.insert(client);
  m_clients.try_emplace(name, client);

  // initialize client data
  int32_t x;
  int32_t y;
  client->getCursorPos(x, y);
  client->setJumpCursorPos(x, y);

  // tell primary client about the active sides
  m_primaryClient->reconfigure(getActivePrimarySides());

  return true;
}

bool Server::removeClient(BaseClientProxy *client)
{
  using enum EventTypes;
  // return false if not in list
  ClientSet::iterator i = m_clientSet.find(client);
  if (i == m_clientSet.end()) {
    return false;
  }

  if (client == m_switchBackGuardScreen || client == m_switchBackGuardTarget) {
    clearSwitchBackGuard();
  }
  if (client == m_noNeighborEdgeGuardScreen) {
    clearNoNeighborEdgeGuard();
  }
  m_clipboardPublicationAuthority.removeScreen(getName(client));

  // remove event handlers
  m_events->removeHandler(ScreenShapeChanged, client->getEventTarget());
  m_events->removeHandler(ScreenInfoChanged, client->getEventTarget());
  m_events->removeHandler(ClipboardGrabbed, client->getEventTarget());
  m_events->removeHandler(ClipboardChanged, client->getEventTarget());

  // remove from list
  m_clients.erase(getName(client));
  m_clientSet.erase(i);

  return true;
}

void Server::closeClient(BaseClientProxy *client, const char *msg)
{
  assert(client != m_primaryClient);
  assert(msg != nullptr);

  // send message to client.  this message should cause the client
  // to disconnect.  we add this client to the closed client list
  // and install a timer to remove the client if it doesn't respond
  // quickly enough.  we also remove the client from the active
  // client list since we're not going to listen to it anymore.
  // note that this method also works on clients that are not in
  // the m_clients list.  adoptClient() may call us with such a
  // client.
  LOG_INFO("disconnecting client \"%s\"", getName(client).c_str());

  // send message
  // FIXME -- avoid type cast (kinda hard, though)
  auto clientProxy = static_cast<ClientProxy *>(client);
  clientProxy->close(msg);

  // install timer.  wait timeout seconds for client to close.
  double timeout = 5.0;
  EventQueueTimer *timer = m_events->newOneShotTimer(timeout, nullptr);
  m_events->addHandler(EventTypes::Timer, timer, [this, client](const auto &) { handleClientCloseTimeout(client); });

  // move client to closing list
  removeClient(client);

  m_oldClients.try_emplace(client, timer);

  // if this client is the active screen then we have to
  // jump off of it
  forceLeaveClient(client, "client-close-request");
}

void Server::closeClients(const ServerConfig &config)
{
  // collect the clients that are connected but are being dropped
  // from the configuration (or who's canonical name is changing).
  using RemovedClients = std::set<BaseClientProxy *>;
  RemovedClients removed;
  for (auto index = m_clients.begin(); index != m_clients.end(); ++index) {
    if (!config.isCanonicalName(index->first)) {
      removed.insert(index->second);
    }
  }

  // don't close the primary client
  removed.erase(m_primaryClient);

  // now close them.  we collect the list then close in two steps
  // because closeClient() modifies the collection we iterate over.
  for (auto &client : removed) {
    closeClient(client, kMsgCClose);
  }
}

void Server::removeActiveClient(BaseClientProxy *client)
{
  if (removeClient(client)) {
    forceLeaveClient(client, "client-disconnected");
    m_events->removeHandler(EventTypes::ClientProxyDisconnected, client);
    if (m_clients.size() == 1 && m_oldClients.empty()) {
      m_events->addEvent(Event(EventTypes::ServerDisconnected, this));
    }
  }
}

void Server::removeOldClient(BaseClientProxy *client)
{
  using enum EventTypes;
  OldClients::iterator i = m_oldClients.find(client);
  if (i != m_oldClients.end()) {
    m_events->removeHandler(ClientProxyDisconnected, client);
    m_events->removeHandler(Timer, i->second);
    m_events->deleteTimer(i->second);
    m_oldClients.erase(i);
    if (m_clients.size() == 1 && m_oldClients.empty()) {
      m_events->addEvent(Event(ServerDisconnected, this));
    }
  }
}

void Server::forceLeaveClient(const BaseClientProxy *client, const char *reason)
{
  if (reason == nullptr) {
    reason = "unknown";
  }

  if (client == m_switchScreen) {
    stopSwitch();
  }

  if (const auto *active = (m_activeSaver != nullptr) ? m_activeSaver : m_active; active == client) {
    // record new position (center of primary screen)
    m_primaryClient->getCursorCenter(m_x, m_y);

    // don't notify active screen since it has probably already
    // disconnected.
    LOG_INFO(
        "active client \"%s\" was removed; returning to \"%s\" at %d,%d reason=%s", getName(active).c_str(),
        getName(m_primaryClient).c_str(), m_x, m_y, reason
    );

    // cut over
    m_active = m_primaryClient;
    m_clipboardPublicationAuthority.recordFocus(getName(m_primaryClient), m_seqNum);

    // enter new screen (unless we already have because of the
    // screen saver)
    if (m_activeSaver == nullptr) {
      m_primaryClient->enter(m_x, m_y, m_seqNum, m_primaryClient->getToggleMask(), false);
    }
  }

  // if this screen had the cursor when the screen saver activated
  // then we can't switch back to it when the screen saver
  // deactivates.
  if (m_activeSaver == client) {
    m_activeSaver = nullptr;
  }

  // tell primary client about the active sides
  m_primaryClient->reconfigure(getActivePrimarySides());
}
