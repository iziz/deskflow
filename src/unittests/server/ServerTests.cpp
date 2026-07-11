/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/EdgeSwitchGeometry.h"
#include "server/Server.h"
#include "server/SwitchBackGuard.h"

#include <chrono>

using deskflow::server::EdgeSwitchBounds;
using deskflow::server::EdgeSwitchPosition;
using deskflow::server::makeEdgeSwitchDirections;
using deskflow::server::makeEdgeSwitchProbe;
using deskflow::server::shouldCacheNoNeighborMiss;
using deskflow::server::SwitchBackGuard;
using namespace std::chrono_literals;

void ServerTests::SwitchToScreenInfo_alloc_screen()
{
  auto actual = new Server::SwitchToScreenInfo("test");
  QCOMPARE(actual->m_screen, "test");
  delete actual;
}

void ServerTests::KeyboardBroadcastInfo_alloc_stateAndSceens()
{
  auto info = new Server::KeyboardBroadcastInfo(Server::KeyboardBroadcastInfo::State::kOn, "test");
  QCOMPARE(info->m_state, Server::KeyboardBroadcastInfo::State::kOn);
  QCOMPARE(info->m_screens, "test");
  delete info;
}

void ServerTests::switchBackGuard_releasesNearPerpendicularEdge()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 0, start);

  QVERIFY(!guard.update(bounds, 2940, 0, start + 10ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2900, 0, start + 50ms).shouldRelease());
  const auto result = guard.update(bounds, 2860, 0, start + 90ms);

  QVERIFY(result.shouldRelease());
  QCOMPARE(result.reason, SwitchBackGuard::ReleaseReason::MotionSettled);
}

void ServerTests::switchBackGuard_blocksFastDirectionReversal()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!guard.update(bounds, 2800, 700, start + 10ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2300, 700, start + 60ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2400, 700, start + 70ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2500, 700, start + 80ms).shouldRelease());
  const auto result = guard.update(bounds, 2600, 700, start + 90ms);

  QVERIFY(!result.shouldRelease());
  QVERIFY(result.towardVelocity > SwitchBackGuard::MaximumTowardVelocity);
}

void ServerTests::switchBackGuard_releasesSlowDirectionReversal()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!guard.update(bounds, 2800, 700, start + 10ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2300, 700, start + 60ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2200, 700, start + 70ms).shouldRelease());
  const auto result = guard.update(bounds, 2202, 700, start + 90ms);

  QVERIFY(result.shouldRelease());
  QVERIFY(result.towardVelocity <= SwitchBackGuard::MaximumTowardVelocity);
}

void ServerTests::switchBackGuard_isPollingRateIndependent()
{
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  SwitchBackGuard highRate;
  SwitchBackGuard lowRate;
  highRate.arm(Direction::Right, 2991, 700, start);
  lowRate.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!highRate.update(bounds, 2800, 700, start + 10ms).shouldRelease());
  QVERIFY(!highRate.update(bounds, 2300, 700, start + 60ms).shouldRelease());
  SwitchBackGuard::UpdateResult highRateResult;
  for (int i = 1; i <= 30; ++i) {
    highRateResult = highRate.update(bounds, 2300 + i, 700, start + 60ms + std::chrono::milliseconds(i));
    QVERIFY(!highRateResult.shouldRelease());
  }

  QVERIFY(!lowRate.update(bounds, 2800, 700, start + 10ms).shouldRelease());
  QVERIFY(!lowRate.update(bounds, 2300, 700, start + 60ms).shouldRelease());
  SwitchBackGuard::UpdateResult lowRateResult;
  for (int i = 1; i <= 4; ++i) {
    lowRateResult = lowRate.update(bounds, 2300 + (8 * i), 700, start + 60ms + std::chrono::milliseconds(8 * i));
    QVERIFY(!lowRateResult.shouldRelease());
  }

  QVERIFY(highRateResult.towardVelocity > SwitchBackGuard::MaximumTowardVelocity);
  QVERIFY(lowRateResult.towardVelocity > SwitchBackGuard::MaximumTowardVelocity);
}

void ServerTests::switchBackGuard_expiresAtBlockedEdge()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  const auto result = guard.update(bounds, 3007, 700, start + SwitchBackGuard::MaximumDuration);

  QVERIFY(result.shouldRelease());
  QCOMPARE(result.reason, SwitchBackGuard::ReleaseReason::Expired);
}

void ServerTests::switchBackGuard_resetsEvidenceAfterSampleGap()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!guard.update(bounds, 2600, 700, start + 10ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 2500, 700, start + 50ms).shouldRelease());

  const auto firstAfterGap = guard.update(bounds, 2600, 700, start + 160ms);
  QVERIFY(!firstAfterGap.shouldRelease());
  QCOMPARE(firstAfterGap.awayDuration, 0ms);

  QVERIFY(!guard.update(bounds, 2650, 700, start + 210ms).shouldRelease());
  const auto settled = guard.update(bounds, 2650, 700, start + 280ms);
  QVERIFY(settled.shouldRelease());
  QCOMPARE(settled.reason, SwitchBackGuard::ReleaseReason::MotionSettled);
}

void ServerTests::switchBackGuard_resetsEvidenceAfterCursorResync()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!guard.update(bounds, 2500, 700, start + 10ms).shouldRelease());
  guard.resynchronize(1500, 700, start + 70ms);

  const auto firstAfterResync = guard.update(bounds, 1501, 700, start + 80ms);
  QVERIFY(!firstAfterResync.shouldRelease());
  QCOMPARE(firstAfterResync.awayDuration, 0ms);
}

void ServerTests::switchBackGuard_sampleGapDoesNotExtendDeadline()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start);

  QVERIFY(!guard.update(bounds, 2500, 700, start + 10ms).shouldRelease());
  QVERIFY(!guard.update(bounds, 3007, 700, start + 200ms).shouldRelease());
  const auto result = guard.update(bounds, 3007, 700, start + SwitchBackGuard::MaximumDuration);

  QVERIFY(result.shouldRelease());
  QCOMPARE(result.reason, SwitchBackGuard::ReleaseReason::Expired);
}

void ServerTests::switchBackGuard_cursorResyncDoesNotExtendDeadline()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Left, 16, 700, start);

  guard.resynchronize(0, 700, start + 300ms);
  const auto result = guard.update(bounds, 0, 700, start + SwitchBackGuard::MaximumDuration);

  QVERIFY(result.shouldRelease());
  QCOMPARE(result.reason, SwitchBackGuard::ReleaseReason::Expired);
}

void ServerTests::switchBackGuard_separatesDeadlineFromFirstSample()
{
  SwitchBackGuard guard;
  const SwitchBackGuard::Bounds bounds{0, 0, 3008, 1692};
  const auto start = SwitchBackGuard::TimePoint{};
  guard.arm(Direction::Right, 2991, 700, start, start + 300ms);

  const auto motion = guard.update(bounds, 3000, 700, start + 310ms);
  QVERIFY(!motion.shouldRelease());
  QVERIFY(motion.towardVelocity > SwitchBackGuard::MaximumTowardVelocity);

  const auto expired = guard.update(bounds, 3000, 700, start + SwitchBackGuard::MaximumDuration);
  QVERIFY(expired.shouldRelease());
  QCOMPARE(expired.reason, SwitchBackGuard::ReleaseReason::Expired);
}

void ServerTests::edgeSwitchProbe_preservesHorizontalOvershoot()
{
  const EdgeSwitchBounds bounds{10, -20, 3008, 1692};

  const EdgeSwitchPosition right = makeEdgeSwitchProbe(bounds, Direction::Right, {3025, -25});
  QCOMPARE(right.x, 3025);
  QCOMPARE(right.y, -20);

  const EdgeSwitchPosition left = makeEdgeSwitchProbe(bounds, Direction::Left, {-50, 2000});
  QCOMPARE(left.x, -50);
  QCOMPARE(left.y, 1671);
}

void ServerTests::edgeSwitchProbe_preservesVerticalOvershoot()
{
  const EdgeSwitchBounds bounds{10, -20, 3008, 1692};

  const EdgeSwitchPosition top = makeEdgeSwitchProbe(bounds, Direction::Top, {-5, -40});
  QCOMPARE(top.x, 10);
  QCOMPARE(top.y, -40);

  const EdgeSwitchPosition bottom = makeEdgeSwitchProbe(bounds, Direction::Bottom, {4000, 2000});
  QCOMPARE(bottom.x, 3017);
  QCOMPARE(bottom.y, 2000);
}

void ServerTests::edgeSwitchDirections_retainsCornerFallback()
{
  const EdgeSwitchBounds bounds{10, -20, 3008, 1692};

  const auto topRight = makeEdgeSwitchDirections(bounds, {3018, -21});
  QCOMPARE(topRight[0], Direction::Right);
  QCOMPARE(topRight[1], Direction::Top);

  const auto bottomLeft = makeEdgeSwitchDirections(bounds, {9, 1672});
  QCOMPARE(bottomLeft[0], Direction::Left);
  QCOMPARE(bottomLeft[1], Direction::Bottom);

  const auto inside = makeEdgeSwitchDirections(bounds, {10, -20});
  QCOMPARE(inside[0], Direction::NoDirection);
  QCOMPARE(inside[1], Direction::NoDirection);
}

void ServerTests::noNeighborMiss_cacheOnlyUnconfiguredTopology()
{
  QVERIFY(shouldCacheNoNeighborMiss(false));
  QVERIFY(!shouldCacheNoNeighborMiss(true));
}

QTEST_MAIN(ServerTests)
