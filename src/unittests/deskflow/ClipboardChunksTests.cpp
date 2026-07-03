/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2015 - 2016 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "ClipboardChunksTests.h"

#include "base/Log.h"
#include "deskflow/ClipboardChunk.h"
#include "deskflow/ProtocolTypes.h"
#include "deskflow/ProtocolUtil.h"
#include "io/IStream.h"

#include <algorithm>
#include <cstring>

namespace {
class MemoryStream : public deskflow::IStream
{
public:
  void close() override
  {
  }

  uint32_t read(void *buffer, uint32_t size) override
  {
    const auto count = static_cast<uint32_t>(std::min<size_t>(size, m_data.size()));
    if (buffer != nullptr && count != 0) {
      std::memcpy(buffer, m_data.data(), count);
    }
    m_data.erase(0, count);
    return count;
  }

  void write(const void *buffer, uint32_t size) override
  {
    m_data.append(static_cast<const char *>(buffer), size);
  }

  void flush() override
  {
  }
  void shutdownInput() override
  {
  }
  void shutdownOutput() override
  {
  }
  void *getEventTarget() const override
  {
    return nullptr;
  }
  bool isReady() const override
  {
    return !m_data.empty();
  }
  uint32_t getSize() const override
  {
    return static_cast<uint32_t>(m_data.size());
  }

private:
  std::string m_data;
};

void writeChunk(MemoryStream &stream, ClipboardID id, uint32_t sequence, uint8_t mark, const std::string &data)
{
  ProtocolUtil::writef(&stream, kMsgDClipboard + 4, id, sequence, mark, &data);
}
} // namespace

void ClipboardChunksTests::startFormatData()
{
  ClipboardID id = 0;
  uint32_t sequence = 0;
  std::string mockDataSize("10");
  ClipboardChunk *chunk = ClipboardChunk::start(id, sequence, mockDataSize, 7);
  uint32_t temp_m_chunk;
  memcpy(&temp_m_chunk, &(chunk->m_chunk[1]), 4);

  QCOMPARE(chunk->m_chunk[0], id);
  QCOMPARE(temp_m_chunk, sequence);
  QCOMPARE(chunk->m_chunk[5], ChunkType::DataStart);
  QCOMPARE(chunk->m_chunk[6], '1');
  QCOMPARE(chunk->m_chunk[7], '0');
  QCOMPARE(chunk->m_chunk[8], '\0');
  QCOMPARE(chunk->clipboardId(), id);
  QCOMPARE(chunk->generation(), 7u);
  delete chunk;
}

void ClipboardChunksTests::formatDataChunk()
{
  ClipboardID id = 0;
  uint32_t sequence = 1;
  uint32_t temp_m_chunk;
  std::string mockData("mock data");
  ClipboardChunk *chunk = ClipboardChunk::data(id, sequence, mockData, 8);
  memcpy(&temp_m_chunk, &chunk->m_chunk[1], 4);

  QCOMPARE(chunk->m_chunk[0], id);
  QCOMPARE(temp_m_chunk, sequence);
  QCOMPARE(chunk->m_chunk[5], ChunkType::DataChunk);
  QCOMPARE(chunk->m_chunk[6], 'm');
  QCOMPARE(chunk->m_chunk[7], 'o');
  QCOMPARE(chunk->m_chunk[8], 'c');
  QCOMPARE(chunk->m_chunk[9], 'k');
  QCOMPARE(chunk->m_chunk[10], ' ');
  QCOMPARE(chunk->m_chunk[11], 'd');
  QCOMPARE(chunk->m_chunk[12], 'a');
  QCOMPARE(chunk->m_chunk[13], 't');
  QCOMPARE(chunk->m_chunk[14], 'a');
  QCOMPARE(chunk->m_chunk[15], '\0');
  QCOMPARE(chunk->clipboardId(), id);
  QCOMPARE(chunk->generation(), 8u);

  delete chunk;
}

void ClipboardChunksTests::endFormatData()
{
  ClipboardID id = 1;
  uint32_t sequence = 1;
  uint32_t temp_m_chunk;
  ClipboardChunk *chunk = ClipboardChunk::end(id, sequence, 9);
  memcpy(&temp_m_chunk, &chunk->m_chunk[1], 4);

  QCOMPARE(chunk->m_chunk[0], id);
  QCOMPARE(temp_m_chunk, sequence);
  QCOMPARE(chunk->m_chunk[5], ChunkType::DataEnd);
  QCOMPARE(chunk->m_chunk[6], '\0');
  QCOMPARE(chunk->clipboardId(), id);
  QCOMPARE(chunk->generation(), 9u);

  delete chunk;
}

void ClipboardChunksTests::assemblersKeepIndependentState()
{
  Log log;
  MemoryStream firstStream;
  MemoryStream secondStream;
  ClipboardChunkAssembler first;
  ClipboardChunkAssembler second;
  ClipboardID id = kClipboardClipboard;
  uint32_t sequence = 0;

  writeChunk(firstStream, kClipboardClipboard, 10, ChunkType::DataStart, "3");
  QCOMPARE(first.process(&firstStream, id, sequence), TransferState::Started);
  writeChunk(secondStream, kClipboardClipboard, 20, ChunkType::DataStart, "4");
  QCOMPARE(second.process(&secondStream, id, sequence), TransferState::Started);

  writeChunk(firstStream, kClipboardClipboard, 10, ChunkType::DataChunk, "abc");
  QCOMPARE(first.process(&firstStream, id, sequence), TransferState::InProgress);
  writeChunk(secondStream, kClipboardClipboard, 20, ChunkType::DataChunk, "wxyz");
  QCOMPARE(second.process(&secondStream, id, sequence), TransferState::InProgress);

  writeChunk(firstStream, kClipboardClipboard, 10, ChunkType::DataEnd, "");
  QCOMPARE(first.process(&firstStream, id, sequence), TransferState::Finished);
  QCOMPARE(first.data(), std::string("abc"));
  writeChunk(secondStream, kClipboardClipboard, 20, ChunkType::DataEnd, "");
  QCOMPARE(second.process(&secondStream, id, sequence), TransferState::Finished);
  QCOMPARE(second.data(), std::string("wxyz"));

  first.reset();
  writeChunk(firstStream, kClipboardClipboard, 10, ChunkType::DataChunk, "stale");
  QCOMPARE(first.process(&firstStream, id, sequence), TransferState::Error);
  QVERIFY(first.data().empty());
}

QTEST_MAIN(ClipboardChunksTests)
