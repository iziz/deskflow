/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "server/EdgeSwitchGeometry.h"

#include <cstdint>
#include <string>
#include <string_view>

class BaseClientProxy;

namespace deskflow::server {

enum class EdgeLookupKind : uint8_t
{
  LegacyLink,
  PhysicalLayout
};

enum class NeighborMapStatus : uint8_t
{
  Mapped,
  NoConfiguredTopology,
  OutsideLinkedInterval,
  TargetDisconnected,
  PhysicalLayoutGap
};

struct EdgeLookupFacts
{
  EdgeLookupKind kind;
  bool hasConfiguredTopology;
  bool hasConfiguredTargetAtPosition;
  bool hasConnectedTargetAtPosition;
};

void observeConfiguredPhysicalCandidate(EdgeLookupFacts &facts, bool containsPosition);
void observeConnectedPhysicalCandidateAtPosition(EdgeLookupFacts &facts);

class NeighborMapResult
{
public:
  static NeighborMapResult mapped(BaseClientProxy &target, EdgeSwitchPosition position);
  static NeighborMapResult miss(NeighborMapStatus status, EdgeSwitchPosition position);

  [[nodiscard]] NeighborMapStatus status() const
  {
    return m_status;
  }

  [[nodiscard]] BaseClientProxy *target() const
  {
    return m_target;
  }

  [[nodiscard]] const EdgeSwitchPosition &position() const
  {
    return m_position;
  }

  [[nodiscard]] bool isMapped() const
  {
    return m_status == NeighborMapStatus::Mapped && m_target != nullptr;
  }

private:
  NeighborMapResult(NeighborMapStatus status, BaseClientProxy *target, EdgeSwitchPosition position);

  NeighborMapStatus m_status;
  BaseClientProxy *m_target;
  EdgeSwitchPosition m_position;
};

NeighborMapStatus classifyNeighborLookup(const EdgeLookupFacts &facts);
NeighborMapStatus mergeNeighborMiss(NeighborMapStatus legacyStatus, NeighborMapStatus physicalStatus);
bool shouldCacheNeighborMiss(NeighborMapStatus status);
std::string_view neighborMapStatusKeyword(NeighborMapStatus status);

enum class SwitchPolicyDecision : uint8_t
{
  Allowed,
  Deferred,
  Blocked
};

enum class SwitchPolicyCondition : uint8_t
{
  None = 0,
  TransitionGuard = 1u << 0,
  DoubleTapPending = 1u << 1,
  WaitDelayPending = 1u << 2,
  LockedCorner = 1u << 3,
  LockedToScreen = 1u << 4
};

constexpr SwitchPolicyCondition operator|(SwitchPolicyCondition lhs, SwitchPolicyCondition rhs)
{
  return static_cast<SwitchPolicyCondition>(static_cast<uint8_t>(lhs) | static_cast<uint8_t>(rhs));
}

constexpr SwitchPolicyCondition &operator|=(SwitchPolicyCondition &lhs, SwitchPolicyCondition rhs)
{
  lhs = lhs | rhs;
  return lhs;
}

constexpr bool hasSwitchPolicyCondition(SwitchPolicyCondition conditions, SwitchPolicyCondition condition)
{
  return (static_cast<uint8_t>(conditions) & static_cast<uint8_t>(condition)) != 0;
}

class SwitchPolicyResult
{
public:
  explicit SwitchPolicyResult(SwitchPolicyCondition conditions);

  [[nodiscard]] SwitchPolicyDecision decision() const
  {
    return m_decision;
  }

  [[nodiscard]] SwitchPolicyCondition conditions() const
  {
    return m_conditions;
  }

  [[nodiscard]] bool shouldSwitch() const
  {
    return m_decision == SwitchPolicyDecision::Allowed;
  }

private:
  SwitchPolicyDecision m_decision;
  SwitchPolicyCondition m_conditions;
};

SwitchPolicyDecision classifySwitchPolicy(SwitchPolicyCondition conditions);
std::string_view switchPolicyDecisionKeyword(SwitchPolicyDecision decision);
std::string switchPolicyConditionKeywords(SwitchPolicyCondition conditions);

} // namespace deskflow::server
