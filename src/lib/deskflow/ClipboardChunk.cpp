/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardChunk.h"

#include "base/Log.h"
#include "base/String.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <algorithm>
#include <cstring>

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

TransferState ClipboardChunkAssembler::process(deskflow::IStream *stream, ClipboardID &id, uint32_t &sequence)
{
  using enum TransferState;
  uint8_t mark;
  std::string data;

  if (!ProtocolUtil::readf(stream, kMsgDClipboard + 4, &id, &sequence, &mark, &data)) {
    reset();
    return Error;
  }

  if (mark == ChunkType::DataStart) {
    if (id >= kClipboardEnd) {
      reset();
      return Error;
    }
    m_active = true;
    m_clipboardId = id;
    m_sequence = sequence;
    m_expectedSize = QString::fromStdString(data).toULong();
    LOG_DEBUG("start receiving clipboard data");
    m_data.clear();
    return Started;
  } else if (mark == ChunkType::DataChunk) {
    if (!m_active || id != m_clipboardId || sequence != m_sequence ||
        data.size() > m_expectedSize - std::min(m_expectedSize, m_data.size())) {
      reset();
      return Error;
    }
    m_data.append(data);
    return TransferState::InProgress;
  } else if (mark == ChunkType::DataEnd) {
    if (!m_active || id != m_clipboardId || sequence != m_sequence) {
      reset();
      return Error;
    }
    if (m_expectedSize != m_data.size()) {
      LOG_ERR("corrupted clipboard data, expected size=%d actual size=%d", m_expectedSize, m_data.size());
      reset();
      return Error;
    }
    m_active = false;
    return Finished;
  }

  LOG_ERR("clipboard transmission failed: unknown error");
  reset();
  return Error;
}

void ClipboardChunkAssembler::reset()
{
  m_active = false;
  m_clipboardId = kClipboardClipboard;
  m_sequence = 0;
  m_expectedSize = 0;
  m_data.clear();
}

bool ClipboardChunkAssembler::active() const
{
  return m_active;
}

ClipboardID ClipboardChunkAssembler::clipboardId() const
{
  return m_clipboardId;
}

size_t ClipboardChunkAssembler::expectedSize() const
{
  return m_expectedSize;
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
