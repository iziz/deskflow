/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/EdgeSwitchGeometry.h"

#include <algorithm>

namespace deskflow::server {

EdgeSwitchPosition
makeEdgeSwitchProbe(const EdgeSwitchBounds &bounds, Direction direction, const EdgeSwitchPosition &requested)
{
  EdgeSwitchPosition probe = requested;
  if (bounds.width <= 0 || bounds.height <= 0) {
    return probe;
  }

  switch (direction) {
    using enum Direction;
  case Left:
  case Right:
    probe.y = std::clamp(probe.y, bounds.y, bounds.y + bounds.height - 1);
    break;

  case Top:
  case Bottom:
    probe.x = std::clamp(probe.x, bounds.x, bounds.x + bounds.width - 1);
    break;

  case NoDirection:
    break;
  }

  return probe;
}

EdgeSwitchPosition insetEdgeSwitchDestination(
    const EdgeSwitchBounds &bounds, Direction direction, const EdgeSwitchPosition &requested, int32_t margin,
    bool hasReturnNeighbor
)
{
  EdgeSwitchPosition destination = requested;
  if (!hasReturnNeighbor || margin <= 0 || bounds.width <= 0 || bounds.height <= 0) {
    return destination;
  }

  const int32_t horizontalInset = std::min(margin, bounds.width - 1);
  const int32_t verticalInset = std::min(margin, bounds.height - 1);
  const int32_t maxX = bounds.x + bounds.width - 1;
  const int32_t maxY = bounds.y + bounds.height - 1;

  switch (direction) {
    using enum Direction;
  case Left:
    destination.x = std::min(destination.x, maxX - horizontalInset);
    break;

  case Right:
    destination.x = std::max(destination.x, bounds.x + horizontalInset);
    break;

  case Top:
    destination.y = std::min(destination.y, maxY - verticalInset);
    break;

  case Bottom:
    destination.y = std::max(destination.y, bounds.y + verticalInset);
    break;

  case NoDirection:
    break;
  }

  return destination;
}

std::array<Direction, 2> makeEdgeSwitchDirections(const EdgeSwitchBounds &bounds, const EdgeSwitchPosition &requested)
{
  std::array<Direction, 2> directions{Direction::NoDirection, Direction::NoDirection};
  if (bounds.width <= 0 || bounds.height <= 0) {
    return directions;
  }

  if (requested.x < bounds.x) {
    directions[0] = Direction::Left;
  } else if (requested.x >= bounds.x + bounds.width) {
    directions[0] = Direction::Right;
  }

  if (requested.y < bounds.y) {
    directions[1] = Direction::Top;
  } else if (requested.y >= bounds.y + bounds.height) {
    directions[1] = Direction::Bottom;
  }

  return directions;
}

} // namespace deskflow::server
