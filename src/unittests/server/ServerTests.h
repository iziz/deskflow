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
  void switchBackGuard_releasesNearPerpendicularEdge();
  void switchBackGuard_blocksFastDirectionReversal();
  void switchBackGuard_releasesSlowDirectionReversal();
  void switchBackGuard_isPollingRateIndependent();
  void switchBackGuard_doesNotExpireAtBlockedEdge();
  void switchBackGuard_resetsEvidenceAfterSampleGap();
  void switchBackGuard_resetsEvidenceAfterCursorResync();
};
