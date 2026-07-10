/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "client/ClientInfoUpdateState.h"

bool ClientInfoUpdateState::beginCursorUpdate()
{
  if (m_cursorUpdatePending) {
    m_cursorUpdateDirty = true;
    return false;
  }

  m_cursorUpdatePending = true;
  beginUpdate(Kind::Cursor);
  return true;
}

void ClientInfoUpdateState::beginUpdate(Kind kind)
{
  m_pending.push_back(kind);
  if (kind == Kind::Shape) {
    ++m_pendingShapeUpdates;
  }
}

ClientInfoUpdateState::Acknowledgment ClientInfoUpdateState::acknowledge()
{
  Acknowledgment result;
  if (m_pending.empty()) {
    return result;
  }

  result.matched = true;
  const auto kind = m_pending.front();
  m_pending.pop_front();

  if (kind == Kind::Shape && m_pendingShapeUpdates > 0) {
    --m_pendingShapeUpdates;
  } else if (kind == Kind::Cursor) {
    m_cursorUpdatePending = false;
    if (m_cursorUpdateDirty) {
      m_cursorUpdateDirty = false;
      result.sendLatestCursor = true;
    }
  }

  return result;
}

bool ClientInfoUpdateState::shouldIgnoreMouse() const
{
  return m_pendingShapeUpdates > 0;
}

size_t ClientInfoUpdateState::pendingCount() const
{
  return m_pending.size();
}
