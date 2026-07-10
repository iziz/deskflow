/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <deque>

class ClientInfoUpdateState
{
public:
  enum class Kind
  {
    Query,
    Cursor,
    Shape
  };

  struct Acknowledgment
  {
    bool matched = false;
    bool sendLatestCursor = false;
  };

  bool beginCursorUpdate();
  void beginUpdate(Kind kind);
  Acknowledgment acknowledge();
  bool shouldIgnoreMouse() const;
  size_t pendingCount() const;

private:
  std::deque<Kind> m_pending;
  size_t m_pendingShapeUpdates = 0;
  bool m_cursorUpdatePending = false;
  bool m_cursorUpdateDirty = false;
};
