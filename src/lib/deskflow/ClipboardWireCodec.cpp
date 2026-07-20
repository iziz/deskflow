/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "deskflow/ClipboardWireCodec.h"

#include <QByteArray>
#include <QtEndian>

#include <algorithm>
#include <array>
#include <limits>

namespace deskflow {
namespace {
constexpr std::array<char, 4> kCompressedClipboardMagic{'D', 'F', 'Z', '1'};
constexpr size_t kQtCompressedSizeHeader = sizeof(quint32);

bool hasCompressedMagic(std::string_view payload)
{
  return payload.size() >= kCompressedClipboardMagic.size() &&
         std::equal(kCompressedClipboardMagic.begin(), kCompressedClipboardMagic.end(), payload.begin());
}
} // namespace

std::string encodeClipboardWirePayload(std::string data, bool compressionEnabled)
{
  if (!compressionEnabled || data.empty() || data.size() > std::numeric_limits<quint32>::max()) {
    return data;
  }

  const auto source = QByteArray::fromRawData(data.data(), static_cast<qsizetype>(data.size()));
  const auto compressed = qCompress(source, 6);
  const auto encodedSize = kCompressedClipboardMagic.size() + static_cast<size_t>(compressed.size());
  if (encodedSize >= data.size()) {
    return data;
  }

  std::string encoded(kCompressedClipboardMagic.data(), kCompressedClipboardMagic.size());
  encoded.append(compressed.constData(), static_cast<size_t>(compressed.size()));
  return encoded;
}

ClipboardWireDecodeResult
decodeClipboardWirePayload(std::string_view payload, bool compressionEnabled, size_t maximumDecodedSize)
{
  if (!compressionEnabled || !hasCompressedMagic(payload)) {
    if (payload.size() > maximumDecodedSize) {
      return {};
    }
    return {true, false, std::string(payload)};
  }

  const auto compressed = payload.substr(kCompressedClipboardMagic.size());
  if (compressed.size() < kQtCompressedSizeHeader) {
    return {};
  }

  const auto declaredSize = qFromBigEndian<quint32>(reinterpret_cast<const uchar *>(compressed.data()));
  if (declaredSize == 0 || declaredSize > maximumDecodedSize) {
    return {};
  }

  const auto bytes = QByteArray::fromRawData(compressed.data(), static_cast<qsizetype>(compressed.size()));
  const auto decoded = qUncompress(bytes);
  if (decoded.size() != static_cast<qsizetype>(declaredSize)) {
    return {};
  }

  return {true, true, std::string(decoded.constData(), static_cast<size_t>(decoded.size()))};
}

} // namespace deskflow
