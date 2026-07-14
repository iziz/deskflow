/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2014 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ServerTests.h"

#include "server/ClipboardPublicationAuthority.h"
#include "server/EdgeSwitchGeometry.h"
#include "server/EdgeSwitchTypes.h"
#include "server/Server.h"
#include "server/SwitchBackGuard.h"

#include <array>
#include <chrono>
#include <string>
#include <string_view>

using deskflow::server::classifyNeighborLookup;
using deskflow::server::classifySwitchPolicy;
using deskflow::server::ClipboardPublicationAuthority;
using deskflow::server::EdgeLookupFacts;
using deskflow::server::EdgeLookupKind;
using deskflow::server::EdgeSwitchBounds;
using deskflow::server::EdgeSwitchPosition;
using deskflow::server::makeEdgeSwitchDirections;
using deskflow::server::makeEdgeSwitchProbe;
using deskflow::server::mergeNeighborMiss;
using deskflow::server::NeighborMapStatus;
using deskflow::server::neighborMapStatusKeyword;
using deskflow::server::observeConfiguredPhysicalCandidate;
using deskflow::server::observeConnectedPhysicalCandidateAtPosition;
using deskflow::server::PendingClipboardPublication;
using deskflow::server::shouldCacheNeighborMiss;
using deskflow::server::SwitchBackGuard;
using deskflow::server::SwitchPolicyCondition;
using deskflow::server::switchPolicyConditionKeywords;
using deskflow::server::SwitchPolicyDecision;
using deskflow::server::switchPolicyDecisionKeyword;
using deskflow::server::SwitchPolicyResult;
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

void ServerTests::clipboardPublicationAuthority_acceptsIssuedFocusAfterScreenSwitch()
{
  ClipboardPublicationAuthority authority;
  authority.recordFocus("mac", 381);
  authority.recordFocus("windows", 382);

  QVERIFY(authority.isFocusValid("mac", 381));
  QCOMPARE(
      authority.evaluate("mac", 381, "windows", 377, "image", "Taxonomy"),
      ClipboardPublicationAuthority::Decision::Commit
  );
}

void ServerTests::clipboardPublicationAuthority_rejectsForgedFocusAndRetainsIssuedHistory()
{
  ClipboardPublicationAuthority authority;
  authority.recordFocus("mac", 381);

  QVERIFY(!authority.isFocusValid("mac", 382));
  authority.recordFocus("windows", 382);
  authority.recordFocus("mac", 383);
  QVERIFY(authority.isFocusValid("mac", 381));
  QVERIFY(!authority.isFocusValid("mac", 382));
  QVERIFY(authority.isFocusValid("mac", 383));

  authority.removeScreen("mac");
  QVERIFY(!authority.isFocusValid("mac", 381));
  QVERIFY(!authority.isFocusValid("mac", 383));
}

void ServerTests::clipboardPublicationAuthority_ordersConcurrentPublications()
{
  ClipboardPublicationAuthority authority;
  authority.recordFocus("mac", 381);
  authority.recordFocus("windows", 382);

  QCOMPARE(
      authority.evaluate("mac", 381, "windows", 382, "newer", "older"),
      ClipboardPublicationAuthority::Decision::Superseded
  );
  QCOMPARE(
      authority.evaluate("windows", 382, "mac", 381, "older", "newer"), ClipboardPublicationAuthority::Decision::Commit
  );
}

void ServerTests::clipboardPublicationAuthority_detectsIdempotentRetry()
{
  ClipboardPublicationAuthority authority;
  authority.recordFocus("mac", 381);

  QCOMPARE(
      authority.evaluate("mac", 381, "mac", 381, "Taxonomy", "Taxonomy"),
      ClipboardPublicationAuthority::Decision::Duplicate
  );
  QCOMPARE(
      authority.evaluate("mac", 381, "mac", 381, "Taxonomy", "Classification"),
      ClipboardPublicationAuthority::Decision::Commit
  );
}

void ServerTests::pendingClipboardPublication_resolvesOnlyMatchingCommit()
{
  PendingClipboardPublication publication;

  QVERIFY(publication.begin(kClipboardClipboard, 381, 17));
  QVERIFY(publication.active());
  QVERIFY(publication.matches(kClipboardClipboard, 381));
  QVERIFY(!publication.resolve(kClipboardClipboard, 382).has_value());
  const auto transferId = publication.resolve(kClipboardClipboard, 381);
  QVERIFY(transferId.has_value());
  QCOMPARE(*transferId, 17u);
  QVERIFY(!publication.active());
}

void ServerTests::pendingClipboardPublication_cancellationPreventsLateCommit()
{
  PendingClipboardPublication publication;

  QVERIFY(publication.begin(kClipboardClipboard, 381, 17));
  QVERIFY(!publication.begin(kClipboardClipboard, 382, 18));
  publication.cancel(17);
  QVERIFY(!publication.resolve(kClipboardClipboard, 381).has_value());
  QVERIFY(publication.begin(kClipboardClipboard, 382, 18));
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

void ServerTests::neighborLookup_classifiesFacts()
{
  struct TestCase
  {
    EdgeLookupFacts facts;
    NeighborMapStatus expected;
  };
  const std::array cases = {
      TestCase{{EdgeLookupKind::LegacyLink, false, false, false}, NeighborMapStatus::NoConfiguredTopology},
      TestCase{{EdgeLookupKind::PhysicalLayout, false, false, false}, NeighborMapStatus::NoConfiguredTopology},
      TestCase{{EdgeLookupKind::LegacyLink, true, false, false}, NeighborMapStatus::OutsideLinkedInterval},
      TestCase{{EdgeLookupKind::PhysicalLayout, true, false, false}, NeighborMapStatus::PhysicalLayoutGap},
      TestCase{{EdgeLookupKind::LegacyLink, true, true, false}, NeighborMapStatus::TargetDisconnected},
      TestCase{{EdgeLookupKind::PhysicalLayout, true, true, false}, NeighborMapStatus::TargetDisconnected},
      TestCase{{EdgeLookupKind::LegacyLink, true, true, true}, NeighborMapStatus::Mapped},
      TestCase{{EdgeLookupKind::PhysicalLayout, true, true, true}, NeighborMapStatus::Mapped},
  };

  for (const auto &test : cases) {
    QCOMPARE(classifyNeighborLookup(test.facts), test.expected);
  }
}

void ServerTests::physicalLookupFacts_accumulateCandidateStates()
{
  EdgeLookupFacts facts{EdgeLookupKind::PhysicalLayout, false, false, false};
  QCOMPARE(classifyNeighborLookup(facts), NeighborMapStatus::NoConfiguredTopology);

  observeConfiguredPhysicalCandidate(facts, false);
  QCOMPARE(classifyNeighborLookup(facts), NeighborMapStatus::PhysicalLayoutGap);

  observeConfiguredPhysicalCandidate(facts, true);
  QCOMPARE(classifyNeighborLookup(facts), NeighborMapStatus::TargetDisconnected);

  observeConfiguredPhysicalCandidate(facts, false);
  QCOMPARE(classifyNeighborLookup(facts), NeighborMapStatus::TargetDisconnected);

  observeConnectedPhysicalCandidateAtPosition(facts);
  QCOMPARE(classifyNeighborLookup(facts), NeighborMapStatus::Mapped);

  EdgeLookupFacts connectedFacts{EdgeLookupKind::PhysicalLayout, false, false, false};
  observeConnectedPhysicalCandidateAtPosition(connectedFacts);
  QCOMPARE(classifyNeighborLookup(connectedFacts), NeighborMapStatus::Mapped);
}

void ServerTests::neighborMiss_mergePreservesActiveLookup()
{
  QCOMPARE(
      mergeNeighborMiss(NeighborMapStatus::OutsideLinkedInterval, NeighborMapStatus::TargetDisconnected),
      NeighborMapStatus::OutsideLinkedInterval
  );
  QCOMPARE(
      mergeNeighborMiss(NeighborMapStatus::TargetDisconnected, NeighborMapStatus::PhysicalLayoutGap),
      NeighborMapStatus::TargetDisconnected
  );
  QCOMPARE(
      mergeNeighborMiss(NeighborMapStatus::NoConfiguredTopology, NeighborMapStatus::TargetDisconnected),
      NeighborMapStatus::TargetDisconnected
  );
  QCOMPARE(
      mergeNeighborMiss(NeighborMapStatus::NoConfiguredTopology, NeighborMapStatus::PhysicalLayoutGap),
      NeighborMapStatus::PhysicalLayoutGap
  );
  QCOMPARE(
      mergeNeighborMiss(NeighborMapStatus::NoConfiguredTopology, NeighborMapStatus::NoConfiguredTopology),
      NeighborMapStatus::NoConfiguredTopology
  );
}

void ServerTests::neighborMapStatus_cacheAndKeywords()
{
  struct TestCase
  {
    NeighborMapStatus status;
    std::string_view keyword;
    bool cache;
  };
  constexpr std::array cases = {
      TestCase{NeighborMapStatus::Mapped, "mapped", false},
      TestCase{NeighborMapStatus::NoConfiguredTopology, "no-configured-topology", true},
      TestCase{NeighborMapStatus::OutsideLinkedInterval, "outside-linked-interval", false},
      TestCase{NeighborMapStatus::TargetDisconnected, "target-disconnected", false},
      TestCase{NeighborMapStatus::PhysicalLayoutGap, "physical-layout-gap", false},
  };

  for (const auto &test : cases) {
    QCOMPARE(neighborMapStatusKeyword(test.status), test.keyword);
    QCOMPARE(shouldCacheNeighborMiss(test.status), test.cache);
  }
}

void ServerTests::switchPolicy_classifiesConditionsAndKeywords()
{
  const auto pending = SwitchPolicyCondition::DoubleTapPending | SwitchPolicyCondition::WaitDelayPending;
  struct TestCase
  {
    SwitchPolicyCondition conditions;
    SwitchPolicyDecision decision;
    std::string keywords;
  };
  const std::array cases = {
      TestCase{SwitchPolicyCondition::None, SwitchPolicyDecision::Allowed, "none"},
      TestCase{SwitchPolicyCondition::DoubleTapPending, SwitchPolicyDecision::Deferred, "double-tap-pending"},
      TestCase{SwitchPolicyCondition::WaitDelayPending, SwitchPolicyDecision::Deferred, "wait-delay-pending"},
      TestCase{pending, SwitchPolicyDecision::Deferred, "double-tap-pending,wait-delay-pending"},
      TestCase{SwitchPolicyCondition::TransitionGuard, SwitchPolicyDecision::Blocked, "transition-guard"},
      TestCase{SwitchPolicyCondition::LockedCorner, SwitchPolicyDecision::Blocked, "locked-corner"},
      TestCase{SwitchPolicyCondition::LockedToScreen, SwitchPolicyDecision::Blocked, "locked-to-screen"},
      TestCase{
          SwitchPolicyCondition::TransitionGuard | SwitchPolicyCondition::WaitDelayPending,
          SwitchPolicyDecision::Blocked,
          "transition-guard,wait-delay-pending",
      },
      TestCase{
          SwitchPolicyCondition::TransitionGuard | SwitchPolicyCondition::DoubleTapPending |
              SwitchPolicyCondition::WaitDelayPending | SwitchPolicyCondition::LockedCorner |
              SwitchPolicyCondition::LockedToScreen,
          SwitchPolicyDecision::Blocked,
          "transition-guard,double-tap-pending,wait-delay-pending,locked-corner,locked-to-screen",
      },
  };

  for (const auto &test : cases) {
    QCOMPARE(classifySwitchPolicy(test.conditions), test.decision);
    QCOMPARE(switchPolicyConditionKeywords(test.conditions), test.keywords);
    const SwitchPolicyResult result{test.conditions};
    QCOMPARE(result.decision(), test.decision);
    QCOMPARE(result.conditions(), test.conditions);
    QCOMPARE(result.shouldSwitch(), test.decision == SwitchPolicyDecision::Allowed);
  }

  QCOMPARE(switchPolicyDecisionKeyword(SwitchPolicyDecision::Allowed), std::string_view{"allowed"});
  QCOMPARE(switchPolicyDecisionKeyword(SwitchPolicyDecision::Deferred), std::string_view{"deferred"});
  QCOMPARE(switchPolicyDecisionKeyword(SwitchPolicyDecision::Blocked), std::string_view{"blocked"});
}

QTEST_MAIN(ServerTests)
