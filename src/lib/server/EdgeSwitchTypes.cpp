/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/EdgeSwitchTypes.h"

#include <array>
#include <cassert>
#include <utility>

namespace deskflow::server {

NeighborMapResult::NeighborMapResult(NeighborMapStatus status, BaseClientProxy *target, EdgeSwitchPosition position)
    : m_status(status),
      m_target(target),
      m_position(position)
{
}

NeighborMapResult NeighborMapResult::mapped(BaseClientProxy &target, EdgeSwitchPosition position)
{
  return NeighborMapResult(NeighborMapStatus::Mapped, &target, position);
}

NeighborMapResult NeighborMapResult::miss(NeighborMapStatus status, EdgeSwitchPosition position)
{
  if (status == NeighborMapStatus::Mapped) {
    assert(false && "mapped status requires a target");
    status = NeighborMapStatus::TargetDisconnected;
  }
  return NeighborMapResult(status, nullptr, position);
}

void observeConfiguredPhysicalCandidate(EdgeLookupFacts &facts, bool containsPosition)
{
  assert(facts.kind == EdgeLookupKind::PhysicalLayout);
  facts.hasConfiguredTopology = true;
  facts.hasConfiguredTargetAtPosition |= containsPosition;
}

void observeConnectedPhysicalCandidateAtPosition(EdgeLookupFacts &facts)
{
  assert(facts.kind == EdgeLookupKind::PhysicalLayout);
  facts.hasConfiguredTopology = true;
  facts.hasConfiguredTargetAtPosition = true;
  facts.hasConnectedTargetAtPosition = true;
}

NeighborMapStatus classifyNeighborLookup(const EdgeLookupFacts &facts)
{
  if (!facts.hasConfiguredTopology) {
    return NeighborMapStatus::NoConfiguredTopology;
  }
  if (!facts.hasConfiguredTargetAtPosition) {
    return facts.kind == EdgeLookupKind::PhysicalLayout ? NeighborMapStatus::PhysicalLayoutGap
                                                        : NeighborMapStatus::OutsideLinkedInterval;
  }
  if (!facts.hasConnectedTargetAtPosition) {
    return NeighborMapStatus::TargetDisconnected;
  }
  return NeighborMapStatus::Mapped;
}

NeighborMapStatus mergeNeighborMiss(NeighborMapStatus legacyStatus, NeighborMapStatus physicalStatus)
{
  if (legacyStatus == NeighborMapStatus::Mapped || physicalStatus == NeighborMapStatus::Mapped) {
    assert(false && "mergeNeighborMiss accepts miss statuses only");
    return NeighborMapStatus::TargetDisconnected;
  }
  if (legacyStatus != NeighborMapStatus::NoConfiguredTopology) {
    return legacyStatus;
  }
  return physicalStatus;
}

bool shouldCacheNeighborMiss(NeighborMapStatus status)
{
  return status == NeighborMapStatus::NoConfiguredTopology;
}

std::string_view neighborMapStatusKeyword(NeighborMapStatus status)
{
  switch (status) {
  case NeighborMapStatus::Mapped:
    return "mapped";
  case NeighborMapStatus::NoConfiguredTopology:
    return "no-configured-topology";
  case NeighborMapStatus::OutsideLinkedInterval:
    return "outside-linked-interval";
  case NeighborMapStatus::TargetDisconnected:
    return "target-disconnected";
  case NeighborMapStatus::PhysicalLayoutGap:
    return "physical-layout-gap";
  }
  return "unknown";
}

SwitchPolicyDecision classifySwitchPolicy(SwitchPolicyCondition conditions)
{
  constexpr auto terminalConditions = SwitchPolicyCondition::TransitionGuard | SwitchPolicyCondition::LockedCorner |
                                      SwitchPolicyCondition::LockedToScreen;
  if ((static_cast<uint8_t>(conditions) & static_cast<uint8_t>(terminalConditions)) != 0) {
    return SwitchPolicyDecision::Blocked;
  }
  if (conditions != SwitchPolicyCondition::None) {
    return SwitchPolicyDecision::Deferred;
  }
  return SwitchPolicyDecision::Allowed;
}

SwitchPolicyResult::SwitchPolicyResult(SwitchPolicyCondition conditions)
    : m_decision(classifySwitchPolicy(conditions)),
      m_conditions(conditions)
{
}

std::string_view switchPolicyDecisionKeyword(SwitchPolicyDecision decision)
{
  switch (decision) {
  case SwitchPolicyDecision::Allowed:
    return "allowed";
  case SwitchPolicyDecision::Deferred:
    return "deferred";
  case SwitchPolicyDecision::Blocked:
    return "blocked";
  }
  return "unknown";
}

std::string switchPolicyConditionKeywords(SwitchPolicyCondition conditions)
{
  if (conditions == SwitchPolicyCondition::None) {
    return "none";
  }

  constexpr std::array entries = {
      std::pair{SwitchPolicyCondition::TransitionGuard, std::string_view{"transition-guard"}},
      std::pair{SwitchPolicyCondition::DoubleTapPending, std::string_view{"double-tap-pending"}},
      std::pair{SwitchPolicyCondition::WaitDelayPending, std::string_view{"wait-delay-pending"}},
      std::pair{SwitchPolicyCondition::LockedCorner, std::string_view{"locked-corner"}},
      std::pair{SwitchPolicyCondition::LockedToScreen, std::string_view{"locked-to-screen"}},
  };

  std::string result;
  for (const auto &[condition, keyword] : entries) {
    if (!hasSwitchPolicyCondition(conditions, condition)) {
      continue;
    }
    if (!result.empty()) {
      result += ',';
    }
    result += keyword;
  }
  return result;
}

} // namespace deskflow::server
