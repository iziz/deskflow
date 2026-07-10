/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/SwitchBackGuard.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace deskflow::server {

void SwitchBackGuard::arm(Direction blockedDirection, int32_t x, int32_t y, TimePoint now)
{
  clear();
  if (blockedDirection == Direction::NoDirection) {
    return;
  }

  m_direction = blockedDirection;
  m_samples.push_back({now, axisPosition(x, y)});
}

void SwitchBackGuard::resynchronize(int32_t x, int32_t y, TimePoint now)
{
  if (!isArmed()) {
    return;
  }

  m_awaySince.reset();
  m_samples.clear();
  m_samples.push_back({now, axisPosition(x, y)});
}

void SwitchBackGuard::clear()
{
  m_direction = Direction::NoDirection;
  m_awaySince.reset();
  m_samples.clear();
}

bool SwitchBackGuard::isArmed() const
{
  return m_direction != Direction::NoDirection;
}

Direction SwitchBackGuard::direction() const
{
  return m_direction;
}

SwitchBackGuard::UpdateResult SwitchBackGuard::update(const Bounds &bounds, int32_t x, int32_t y, TimePoint now)
{
  UpdateResult result;
  if (!isArmed()) {
    return result;
  }

  const int32_t position = axisPosition(x, y);
  if (resetAfterSampleGap(position, now)) {
    result.awayFromBlockedEdge = isAwayFromBlockedEdge(bounds, x, y);
    if (result.awayFromBlockedEdge) {
      m_awaySince = now;
    }
    return result;
  }

  const MotionSample previous = m_samples.empty() ? MotionSample{now, position} : m_samples.back();
  recordSample(position, now);

  const double instantaneousTowardVelocity = towardVelocity(previous, m_samples.back());
  const double windowTowardVelocity = towardVelocity(m_samples.front(), m_samples.back());
  result.towardVelocity = std::max(instantaneousTowardVelocity, windowTowardVelocity);

  result.awayFromBlockedEdge = isAwayFromBlockedEdge(bounds, x, y);
  if (!result.awayFromBlockedEdge) {
    m_awaySince.reset();
    return result;
  }

  if (!m_awaySince.has_value()) {
    m_awaySince = now;
  }

  result.awayDuration = std::chrono::duration_cast<std::chrono::milliseconds>(now - *m_awaySince);
  if (result.awayDuration >= ReleaseDwell && result.towardVelocity <= MaximumTowardVelocity) {
    result.reason = ReleaseReason::MotionSettled;
  }

  return result;
}

int32_t SwitchBackGuard::releaseMargin(int32_t edgeLength)
{
  return std::min(ReleaseMargin, std::max<int32_t>(1, edgeLength / 4));
}

const char *SwitchBackGuard::releaseReasonName(ReleaseReason reason)
{
  switch (reason) {
  case ReleaseReason::None:
    return "none";
  case ReleaseReason::MotionSettled:
    return "motion-settled";
  }

  return "unknown";
}

int32_t SwitchBackGuard::axisPosition(int32_t x, int32_t y) const
{
  const bool horizontal = m_direction == Direction::Left || m_direction == Direction::Right;
  return horizontal ? x : y;
}

bool SwitchBackGuard::isAwayFromBlockedEdge(const Bounds &bounds, int32_t x, int32_t y) const
{
  switch (m_direction) {
    using enum Direction;
  case Left:
    return x >= bounds.x + releaseMargin(bounds.width);
  case Right:
    return x <= bounds.x + bounds.width - 1 - releaseMargin(bounds.width);
  case Top:
    return y >= bounds.y + releaseMargin(bounds.height);
  case Bottom:
    return y <= bounds.y + bounds.height - 1 - releaseMargin(bounds.height);
  case NoDirection:
    return false;
  }

  return false;
}

double SwitchBackGuard::towardVelocity(const MotionSample &first, const MotionSample &last) const
{
  const auto elapsed = std::chrono::duration<double>(last.time - first.time).count();
  const int32_t delta = last.position - first.position;
  if (elapsed <= 0.0) {
    if (delta == 0) {
      return 0.0;
    }
    return std::copysign(std::numeric_limits<double>::infinity(), static_cast<double>(delta));
  }

  const double velocity = static_cast<double>(delta) / elapsed;
  const bool negativeIsToward = m_direction == Direction::Left || m_direction == Direction::Top;
  return negativeIsToward ? -velocity : velocity;
}

bool SwitchBackGuard::resetAfterSampleGap(int32_t position, TimePoint now)
{
  if (m_samples.empty() || now - m_samples.back().time <= MaximumSampleGap) {
    return false;
  }

  m_samples.clear();
  m_awaySince.reset();
  recordSample(position, now);
  return true;
}

void SwitchBackGuard::recordSample(int32_t position, TimePoint now)
{
  m_samples.push_back({now, position});
  const auto cutoff = now - VelocityWindow;
  while (m_samples.size() > 2 && m_samples[1].time <= cutoff) {
    m_samples.pop_front();
  }
}

} // namespace deskflow::server
