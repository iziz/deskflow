/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include "deskflow/ProtocolTypes.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <string_view>
#include <utility>

namespace deskflow {

enum class ClipboardChannelHelloKind
{
  Control,
  Clipboard,
  Invalid
};

// Classify only the packet that contained the standard hello-back prefix.
// A following packet must never be interpreted as a role extension.
constexpr ClipboardChannelHelloKind classifyClipboardChannelHello(uint32_t packetSize, uint32_t clientNameSize)
{
  constexpr uint32_t kProtocolNameSize = 7;
  constexpr uint32_t kIntegerFieldsSize = 2 + 2;
  constexpr uint32_t kStringLengthSize = sizeof(uint32_t);
  constexpr uint32_t kChannelRoleSize = 4;
  const auto controlSize = kProtocolNameSize + kIntegerFieldsSize + kStringLengthSize + clientNameSize;
  const auto clipboardSize = controlSize + kChannelRoleSize + kStringLengthSize + kClipboardChannelTokenSize;

  if (packetSize == controlSize) {
    return ClipboardChannelHelloKind::Control;
  }
  if (packetSize == clipboardSize) {
    return ClipboardChannelHelloKind::Clipboard;
  }
  return ClipboardChannelHelloKind::Invalid;
}

class ClipboardChannelToken
{
public:
  using Clock = std::chrono::steady_clock;
  using TimePoint = Clock::time_point;

  void issue(std::string value, TimePoint expiresAt)
  {
    if (value.size() != kClipboardChannelTokenSize) {
      clear();
      return;
    }
    m_value = std::move(value);
    m_expiresAt = expiresAt;
  }

  [[nodiscard]] const std::string &value() const
  {
    return m_value;
  }

  bool consume(std::string_view candidate, TimePoint now)
  {
    if (m_value.size() != kClipboardChannelTokenSize || now > m_expiresAt) {
      clear();
      return false;
    }
    if (candidate.size() != m_value.size()) {
      return false;
    }

    unsigned char difference = 0;
    for (size_t i = 0; i < m_value.size(); ++i) {
      difference |= static_cast<unsigned char>(m_value[i]) ^ static_cast<unsigned char>(candidate[i]);
    }
    if (difference != 0) {
      return false;
    }

    clear();
    return true;
  }

  void clear()
  {
    m_value.clear();
    m_expiresAt = {};
  }

private:
  std::string m_value;
  TimePoint m_expiresAt{};
};

} // namespace deskflow
