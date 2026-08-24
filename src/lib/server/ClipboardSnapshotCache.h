/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ClipboardTypes.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

namespace deskflow::server {

class ClipboardSnapshotCache
{
public:
  using Data = std::shared_ptr<const std::string>;

  Data find(ClipboardID nativeClipboard, uint32_t generation) const;
  Data store(ClipboardID nativeClipboard, uint32_t generation, std::string data);
  void erase(ClipboardID nativeClipboard, uint32_t generation);

private:
  struct Entry
  {
    uint32_t generation = 0;
    Data data;
  };

  std::array<Entry, kClipboardEnd> m_entries;
};

} // namespace deskflow::server
