/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "base/DirectionTypes.h"

#include <array>
#include <cstdint>

namespace deskflow::server {

struct EdgeSwitchBounds
{
  int32_t x;
  int32_t y;
  int32_t width;
  int32_t height;
};

struct EdgeSwitchPosition
{
  int32_t x;
  int32_t y;
};

// Preserve overshoot along the transition axis while keeping the edge
// fraction probe inside the source screen on the perpendicular axis.
EdgeSwitchPosition
makeEdgeSwitchProbe(const EdgeSwitchBounds &bounds, Direction direction, const EdgeSwitchPosition &requested);

// Return edge candidates in the same horizontal-first order used by the
// primary screen while retaining a vertical fallback at corners.
std::array<Direction, 2> makeEdgeSwitchDirections(const EdgeSwitchBounds &bounds, const EdgeSwitchPosition &requested);

// Positional and availability misses must remain live so later motion or a
// reconnect can resolve them without first leaving the edge.
bool shouldCacheNoNeighborMiss(bool hasConfiguredTarget);

} // namespace deskflow::server
