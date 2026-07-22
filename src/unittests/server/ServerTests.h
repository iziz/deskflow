/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include <QTest>

class ServerTests : public QObject
{
  Q_OBJECT
private Q_SLOTS:
  void SwitchToScreenInfo_alloc_screen();
  void KeyboardBroadcastInfo_alloc_stateAndSceens();
  void clipboardPublicationAuthority_acceptsIssuedFocusAfterScreenSwitch();
  void clipboardPublicationAuthority_rejectsForgedFocusAndRetainsIssuedHistory();
  void clipboardPublicationAuthority_boundsIssuedHistory();
  void clipboardPublicationAuthority_ordersConcurrentPublications();
  void clipboardPublicationAuthority_detectsIdempotentRetry();
  void pendingClipboardPublication_resolvesOnlyMatchingCommit();
  void pendingClipboardPublication_cancellationPreventsLateCommit();
  void edgeSwitchProbe_preservesHorizontalOvershoot();
  void edgeSwitchProbe_preservesVerticalOvershoot();
  void edgeSwitchDestination_insetsReturnEdge();
  void edgeSwitchDestination_preservesAsymmetricEdge();
  void edgeSwitchDestination_clampsMarginToSmallScreen();
  void edgeSwitchDirections_retainsCornerFallback();
  void neighborLookup_classifiesFacts();
  void physicalLookupFacts_accumulateCandidateStates();
  void neighborMiss_mergePreservesActiveLookup();
  void neighborMapStatus_cacheAndKeywords();
  void edgeSwitchRouting_blocksLockedCandidates();
  void switchPolicy_classifiesConditionsAndKeywords();
};
