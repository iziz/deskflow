/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace deskflow {

struct ClipboardWireDecodeResult
{
  bool valid = false;
  bool compressed = false;
  std::string data;
};

/**
 * Encodes clipboard data for protocol 1.13 peers. Compression is used only
 * when the encoded payload is smaller than the original clipboard snapshot.
 */
std::string encodeClipboardWirePayload(std::string data, bool compressionEnabled);

/**
 * Decodes clipboard data from a peer and enforces the logical clipboard size
 * limit before allocating the decompressed snapshot.
 */
ClipboardWireDecodeResult
decodeClipboardWirePayload(std::string_view payload, bool compressionEnabled, size_t maximumDecodedSize);

} // namespace deskflow
