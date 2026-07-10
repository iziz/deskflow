/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/DirectionTypes.h"

#include <chrono>
#include <cstdint>
#include <deque>
#include <optional>

namespace deskflow::server {

class SwitchBackGuard
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  struct Bounds
  {
    int32_t x;
    int32_t y;
    int32_t width;
    int32_t height;
  };

  enum class ReleaseReason
  {
    None,
    MotionSettled
  };

  struct UpdateResult
  {
    ReleaseReason reason = ReleaseReason::None;
    bool awayFromBlockedEdge = false;
    double towardVelocity = 0.0;
    std::chrono::milliseconds awayDuration{0};

    bool shouldRelease() const
    {
      return reason != ReleaseReason::None;
    }
  };

  inline static constexpr int32_t ReleaseMargin = 64;
  inline static constexpr auto ReleaseDwell = std::chrono::milliseconds(75);
  inline static constexpr auto VelocityWindow = std::chrono::milliseconds(50);
  inline static constexpr auto MaximumSampleGap = std::chrono::milliseconds(100);
  inline static constexpr double MaximumTowardVelocity = 180.0;

  void arm(Direction blockedDirection, int32_t x, int32_t y, TimePoint now = Clock::now());
  void resynchronize(int32_t x, int32_t y, TimePoint now = Clock::now());
  void clear();

  bool isArmed() const;
  Direction direction() const;
  UpdateResult update(const Bounds &bounds, int32_t x, int32_t y, TimePoint now = Clock::now());

  static int32_t releaseMargin(int32_t edgeLength);
  static const char *releaseReasonName(ReleaseReason reason);

private:
  struct MotionSample
  {
    TimePoint time;
    int32_t position;
  };

  int32_t axisPosition(int32_t x, int32_t y) const;
  bool isAwayFromBlockedEdge(const Bounds &bounds, int32_t x, int32_t y) const;
  double towardVelocity(const MotionSample &first, const MotionSample &last) const;
  bool resetAfterSampleGap(int32_t position, TimePoint now);
  void recordSample(int32_t position, TimePoint now);

  Direction m_direction = Direction::NoDirection;
  std::optional<TimePoint> m_awaySince;
  std::deque<MotionSample> m_samples;
};

} // namespace deskflow::server
