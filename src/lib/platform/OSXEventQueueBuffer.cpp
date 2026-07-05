/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2025 Stephen Jensen <sjensen313@proton.me>
 * SPDX-FileCopyrightText: (C) 2012 - 2025 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2004 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "platform/OSXEventQueueBuffer.h"

#include "base/Event.h"
#include "base/IEventQueue.h"
#include "base/Log.h"

#include <chrono>

namespace {

constexpr auto kEventQueueLogInterval = std::chrono::milliseconds(100);

bool shouldLogEventQueue()
{
  static thread_local auto lastLogTime = std::chrono::steady_clock::time_point{};
  const auto now = std::chrono::steady_clock::now();
  if (now - lastLogTime < kEventQueueLogInterval) {
    return false;
  }
  lastLogTime = now;
  return true;
}

} // namespace

//
// OSXEventQueueBuffer
//

OSXEventQueueBuffer::OSXEventQueueBuffer(IEventQueue *events) : m_eventQueue(events)
{
  // Initialization is now managed using modern constructs
}

void OSXEventQueueBuffer::init()
{
  // No initialization needed for GCD-based implementation
}

void OSXEventQueueBuffer::waitForEvent(double timeout)
{
  std::unique_lock lock(m_mutex);
  if (m_dataQueue.empty()) {
    if (shouldLogEventQueue()) {
      LOG_VERBOSE("waiting for event, timeout: %f seconds", timeout);
    }
    auto end = timeout < 0 ? std::chrono::steady_clock::time_point::max()
                           : std::chrono::steady_clock::now() + std::chrono::duration<double>(timeout);
    m_cond.wait_until(lock, end, [this] { return !m_dataQueue.empty(); });
  } else {
    if (shouldLogEventQueue()) {
      LOG_VERBOSE("found events in the queue");
    }
  }
}

IEventQueueBuffer::Type OSXEventQueueBuffer::getEvent(Event &event, uint32_t &dataID)
{
  std::unique_lock lock(m_mutex);
  if (m_dataQueue.empty()) {
    if (shouldLogEventQueue()) {
      LOG_VERBOSE("no events in queue");
    }
    return IEventQueueBuffer::Type::Unknown;
  }

  dataID = m_dataQueue.front();
  m_dataQueue.pop();
  lock.unlock(); // Unlock early to allow other threads to proceed

  if (shouldLogEventQueue()) {
    LOG_VERBOSE("handled user event with dataID: %u", dataID);
  }
  return IEventQueueBuffer::Type::User;
}

bool OSXEventQueueBuffer::addEvent(uint32_t dataID)
{
  std::scoped_lock lock{m_mutex};
  if (shouldLogEventQueue()) {
    LOG_VERBOSE("adding user event with dataID: %u", dataID);
  }
  m_dataQueue.push(dataID);
  m_cond.notify_one();
  if (shouldLogEventQueue()) {
    LOG_VERBOSE("user event added to queue, dataID=%u", dataID);
  }
  return true;
}

bool OSXEventQueueBuffer::isEmpty() const
{
  std::scoped_lock lock{m_mutex};
  bool empty = m_dataQueue.empty();
  if (shouldLogEventQueue()) {
    LOG_VERBOSE("queue is %s", empty ? "empty" : "not empty");
  }
  return empty;
}
