/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "server/ClipboardSnapshotCache.h"

namespace deskflow::server {

ClipboardSnapshotCache::Data ClipboardSnapshotCache::find(ClipboardID nativeClipboard, uint32_t generation) const
{
  if (nativeClipboard >= kClipboardEnd || generation == 0) {
    return {};
  }

  const auto &entry = m_entries[nativeClipboard];
  return entry.generation == generation ? entry.data : Data{};
}

ClipboardSnapshotCache::Data
ClipboardSnapshotCache::store(ClipboardID nativeClipboard, uint32_t generation, std::string data)
{
  auto snapshot = std::make_shared<const std::string>(std::move(data));
  if (nativeClipboard < kClipboardEnd && generation != 0) {
    m_entries[nativeClipboard] = Entry{generation, snapshot};
  }
  return snapshot;
}

void ClipboardSnapshotCache::erase(ClipboardID nativeClipboard, uint32_t generation)
{
  if (nativeClipboard >= kClipboardEnd || generation == 0) {
    return;
  }

  auto &entry = m_entries[nativeClipboard];
  if (entry.generation == generation) {
    entry = Entry{};
  }
}

} // namespace deskflow::server
