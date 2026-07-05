/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/Chunk.h"
#include "deskflow/ClipboardTypes.h"
#include "deskflow/ProtocolTypes.h"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

constexpr static auto s_clipboardChunkMetaSize = 7;

namespace deskflow {
class IStream;
}

struct ClipboardChunkAssemblyState
{
  size_t expectedSize = 0;
  bool active = false;
  ClipboardID clipboardId = kClipboardClipboard;
  uint32_t sequence = 0;
};

class ClipboardChunk : public Chunk
{
public:
  explicit ClipboardChunk(size_t size, uint64_t generation = 0);

  static ClipboardChunk *start(ClipboardID id, uint32_t sequence, const std::string &size, uint64_t generation = 0);
  static ClipboardChunk *
  data(ClipboardID id, uint32_t sequence, const std::string &data, uint64_t generation = 0);
  static ClipboardChunk *end(ClipboardID id, uint32_t sequence, uint64_t generation = 0);

  static TransferState assemble(
      deskflow::IStream *stream, std::string &dataCached, ClipboardID &id, uint32_t &sequence,
      ClipboardChunkAssemblyState &state, size_t maxDataSize
  );

  static void send(deskflow::IStream *stream, void *data);

  static size_t getExpectedSize(const ClipboardChunkAssemblyState &state)
  {
    return state.expectedSize;
  }

  ClipboardID clipboardId() const;
  uint64_t generation() const;

private:
  uint64_t m_generation = 0;
};

class ClipboardChunkAssembler
{
public:
  TransferState process(
      deskflow::IStream *stream, ClipboardID &id, uint32_t &sequence,
      size_t maxDataSize = std::numeric_limits<size_t>::max()
  );
  void reset();

  bool active() const;
  ClipboardID clipboardId() const;
  size_t expectedSize() const;
  const std::string &data() const;

private:
  ClipboardChunkAssemblyState m_state;
  std::string m_data;
};
