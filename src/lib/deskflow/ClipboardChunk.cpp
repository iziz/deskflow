/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardChunk.h"

#include "base/Log.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <system_error>

namespace {

void clearCachedData(std::string &dataCached)
{
  dataCached.clear();
  dataCached.shrink_to_fit();
}

bool parseExpectedSize(const std::string &data, size_t &expectedSize)
{
  const auto *begin = data.data();
  const auto *end = begin + data.size();
  const auto parsed = std::from_chars(begin, end, expectedSize);
  return parsed.ec == std::errc{} && parsed.ptr == end;
}

bool wouldExceed(size_t currentSize, size_t extraSize, size_t limit)
{
  return currentSize > limit || extraSize > limit - currentSize;
}

} // namespace

ClipboardChunk::ClipboardChunk(size_t size, uint64_t generation) : Chunk(size), m_generation(generation)
{
  m_dataSize = size - s_clipboardChunkMetaSize;
}

ClipboardChunk *ClipboardChunk::start(ClipboardID id, uint32_t sequence, const std::string &size, uint64_t generation)
{
  size_t sizeLength = size.size();
  auto *start = new ClipboardChunk(sizeLength + s_clipboardChunkMetaSize, generation);
  char *chunk = start->m_chunk;

  chunk[0] = id;
  std::memcpy(&chunk[1], &sequence, 4);
  chunk[5] = ChunkType::DataStart;
  memcpy(&chunk[6], size.c_str(), sizeLength);
  chunk[sizeLength + s_clipboardChunkMetaSize - 1] = '\0';

  return start;
}

ClipboardChunk *
ClipboardChunk::data(ClipboardID id, uint32_t sequence, const std::string &data, uint64_t generation)
{
  size_t dataSize = data.size();
  auto *chunk = new ClipboardChunk(dataSize + s_clipboardChunkMetaSize, generation);
  char *chunkData = chunk->m_chunk;

  chunkData[0] = id;
  std::memcpy(&chunkData[1], &sequence, 4);
  chunkData[5] = ChunkType::DataChunk;
  memcpy(&chunkData[6], data.c_str(), dataSize);
  chunkData[dataSize + s_clipboardChunkMetaSize - 1] = '\0';

  return chunk;
}

ClipboardChunk *ClipboardChunk::end(ClipboardID id, uint32_t sequence, uint64_t generation)
{
  auto *end = new ClipboardChunk(s_clipboardChunkMetaSize, generation);
  char *chunk = end->m_chunk;

  chunk[0] = id;
  std::memcpy(&chunk[1], &sequence, 4);
  chunk[5] = ChunkType::DataEnd;
  chunk[s_clipboardChunkMetaSize - 1] = '\0';
  return end;
}

TransferState ClipboardChunk::assemble(
    deskflow::IStream *stream, std::string &dataCached, ClipboardID &id, uint32_t &sequence,
    ClipboardChunkAssemblyState &state, size_t maxDataSize
)
{
  using enum TransferState;
  uint8_t mark;
  std::string data;
  auto reset = [&]() {
    state = {};
    clearCachedData(dataCached);
  };

  if (!ProtocolUtil::readf(stream, kMsgDClipboard + 4, &id, &sequence, &mark, &data)) {
    reset();
    return Error;
  }

  if (id >= kClipboardEnd) {
    LOG_ERR("clipboard chunk invalid id: %d", id);
    reset();
    return Error;
  }

  if (mark == ChunkType::DataStart) {
    size_t expected = 0;
    if (!parseExpectedSize(data, expected)) {
      LOG_ERR("clipboard invalid size header: %s", data.c_str());
      reset();
      return Error;
    }

    clearCachedData(dataCached);
    state.expectedSize = expected;
    state.active = true;
    state.clipboardId = id;
    state.sequence = sequence;

    if (state.expectedSize > maxDataSize) {
      LOG_ERR("clipboard size exceeds limit, size: %zu, limit: %zu", state.expectedSize, maxDataSize);
      reset();
      return Error;
    }

    LOG_DEBUG("start receiving clipboard data, expected size=%zu", state.expectedSize);
    return Started;
  }

  if (!state.active || id != state.clipboardId || sequence != state.sequence) {
    LOG_ERR("clipboard chunk received before matching start");
    reset();
    return Error;
  }

  if (mark == ChunkType::DataChunk) {
    if (wouldExceed(dataCached.size(), data.size(), state.expectedSize)) {
      LOG_ERR(
          "clipboard size exceeds declared, size: %zu, declared: %zu", dataCached.size() + data.size(),
          state.expectedSize
      );
      reset();
      return Error;
    }

    dataCached.append(data);
    return InProgress;
  }

  if (mark == ChunkType::DataEnd) {
    state.active = false;

    if (state.expectedSize != dataCached.size()) {
      LOG_ERR("corrupted clipboard data, expected size=%zu actual size=%zu", state.expectedSize, dataCached.size());
      reset();
      return Error;
    }

    return Finished;
  }

  LOG_ERR("unknown clipboard chunk mark");
  reset();
  return Error;
}

TransferState ClipboardChunkAssembler::process(
    deskflow::IStream *stream, ClipboardID &id, uint32_t &sequence, size_t maxDataSize
)
{
  return ClipboardChunk::assemble(stream, m_data, id, sequence, m_state, maxDataSize);
}

void ClipboardChunkAssembler::reset()
{
  m_state = {};
  clearCachedData(m_data);
}

bool ClipboardChunkAssembler::active() const
{
  return m_state.active;
}

ClipboardID ClipboardChunkAssembler::clipboardId() const
{
  return m_state.clipboardId;
}

size_t ClipboardChunkAssembler::expectedSize() const
{
  return m_state.expectedSize;
}

const std::string &ClipboardChunkAssembler::data() const
{
  return m_data;
}

void ClipboardChunk::send(deskflow::IStream *stream, void *data)
{
  const auto *clipboardData = static_cast<ClipboardChunk *>(data);

  LOG_VERBOSE("sending clipboard chunk");

  const char *chunk = clipboardData->m_chunk;
  ClipboardID id = chunk[0];
  uint32_t sequence;
  std::memcpy(&sequence, &chunk[1], 4);
  uint8_t mark = chunk[5];
  std::string dataChunk(&chunk[6], clipboardData->m_dataSize);

  switch (mark) {
  case ChunkType::DataStart:
    LOG_VERBOSE("sending clipboard chunk start: size=%s", dataChunk.c_str());
    break;

  case ChunkType::DataChunk:
    LOG_VERBOSE("sending clipboard chunk data: size=%i", dataChunk.size());
    break;

  case ChunkType::DataEnd:
    LOG_VERBOSE("sending clipboard finished");
    break;

  default:
    break;
  }

  ProtocolUtil::writef(stream, kMsgDClipboard, id, sequence, mark, &dataChunk);
}

ClipboardID ClipboardChunk::clipboardId() const
{
  return static_cast<ClipboardID>(m_chunk[0]);
}

uint64_t ClipboardChunk::generation() const
{
  return m_generation;
}
