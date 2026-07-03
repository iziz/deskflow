/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Chunk.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/ProtocolTypes.h"

#include <cstdint>
#include <string>

constexpr static auto s_clipboardChunkMetaSize = 7;

namespace deskflow {
class IStream;
}

class ClipboardChunk : public Chunk
{
public:
  explicit ClipboardChunk(size_t size, uint64_t generation = 0);

  static ClipboardChunk *start(ClipboardID id, uint32_t sequence, const std::string &size, uint64_t generation = 0);
  static ClipboardChunk *
  data(ClipboardID id, uint32_t sequence, const std::string &data, uint64_t generation = 0);
  static ClipboardChunk *end(ClipboardID id, uint32_t sequence, uint64_t generation = 0);

  static void send(deskflow::IStream *stream, void *data);

  ClipboardID clipboardId() const;
  uint64_t generation() const;

private:
  uint64_t m_generation = 0;
};

class ClipboardChunkAssembler
{
public:
  TransferState process(deskflow::IStream *stream, ClipboardID &id, uint32_t &sequence);
  void reset();

  bool active() const;
  ClipboardID clipboardId() const;
  size_t expectedSize() const;
  const std::string &data() const;

private:
  bool m_active = false;
  ClipboardID m_clipboardId = kClipboardClipboard;
  uint32_t m_sequence = 0;
  size_t m_expectedSize = 0;
  std::string m_data;
};
