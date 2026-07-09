/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/Server.h"
#include "server/SwitchBackGuard.h"

#include <chrono>

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

QTEST_MAIN(ServerTests)
