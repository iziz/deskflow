/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#pragma once

#include <atomic>
#include <cassert>

namespace deskflow {

// Tracks the OpenSSL operation that must be retried and the socket readiness
// required before retrying it. OpenSSL does not allow a different I/O operation
// to run while an SSL_read or SSL_write retry is pending.
class TlsIoRetry final
{
public:
  enum class Operation
  {
    None,
    Read,
    Write
  };

  enum class WaitFor
  {
    None,
    Readable,
    Writable
  };

  void set(Operation operation, WaitFor waitFor)
  {
    if (operation == Operation::None || waitFor == WaitFor::None) {
      assert(false && "TLS retry requires an operation and poll direction");
      reset();
      return;
    }

    if (const auto pendingOperation = this->operation();
        pendingOperation != Operation::None && pendingOperation != operation) {
      assert(false && "TLS retry cannot switch operations while pending");
      return;
    }

    m_state.store(encode(operation, waitFor), std::memory_order_release);
  }

  void complete(Operation operation)
  {
    if (operation == Operation::None) {
      assert(false && "TLS retry completion requires an operation");
      return;
    }

    const auto pendingOperation = this->operation();
    if (pendingOperation != Operation::None && pendingOperation != operation) {
      assert(false && "TLS retry cannot complete a different operation");
      return;
    }

    if (pendingOperation == operation) {
      reset();
    }
  }

  void reset()
  {
    m_state.store(State::None, std::memory_order_release);
  }

  [[nodiscard]] bool pending() const
  {
    return m_state.load(std::memory_order_acquire) != State::None;
  }

  [[nodiscard]] Operation operation() const
  {
    switch (m_state.load(std::memory_order_acquire)) {
    case State::ReadReadable:
    case State::ReadWritable:
      return Operation::Read;

    case State::WriteReadable:
    case State::WriteWritable:
      return Operation::Write;

    case State::None:
      return Operation::None;
    }

    return Operation::None;
  }

  [[nodiscard]] WaitFor waitFor() const
  {
    switch (m_state.load(std::memory_order_acquire)) {
    case State::ReadReadable:
    case State::WriteReadable:
      return WaitFor::Readable;

    case State::ReadWritable:
    case State::WriteWritable:
      return WaitFor::Writable;

    case State::None:
      return WaitFor::None;
    }

    return WaitFor::None;
  }

  [[nodiscard]] bool wantsRead() const
  {
    return waitFor() == WaitFor::Readable;
  }

  [[nodiscard]] bool wantsWrite() const
  {
    return waitFor() == WaitFor::Writable;
  }

  [[nodiscard]] bool canRetry(Operation operation, bool readable, bool writable) const
  {
    const auto state = m_state.load(std::memory_order_acquire);
    if (decodeOperation(state) != operation) {
      return false;
    }

    switch (decodeWaitFor(state)) {
    case WaitFor::Readable:
      return readable;

    case WaitFor::Writable:
      return writable;

    case WaitFor::None:
      return false;
    }

    return false;
  }

private:
  enum class State : unsigned char
  {
    None,
    ReadReadable,
    ReadWritable,
    WriteReadable,
    WriteWritable
  };

  [[nodiscard]] static constexpr State encode(Operation operation, WaitFor waitFor)
  {
    if (operation == Operation::Read) {
      return waitFor == WaitFor::Readable ? State::ReadReadable : State::ReadWritable;
    }

    return waitFor == WaitFor::Readable ? State::WriteReadable : State::WriteWritable;
  }

  [[nodiscard]] static constexpr Operation decodeOperation(State state)
  {
    switch (state) {
    case State::ReadReadable:
    case State::ReadWritable:
      return Operation::Read;

    case State::WriteReadable:
    case State::WriteWritable:
      return Operation::Write;

    case State::None:
      return Operation::None;
    }

    return Operation::None;
  }

  [[nodiscard]] static constexpr WaitFor decodeWaitFor(State state)
  {
    switch (state) {
    case State::ReadReadable:
    case State::WriteReadable:
      return WaitFor::Readable;

    case State::ReadWritable:
    case State::WriteWritable:
      return WaitFor::Writable;

    case State::None:
      return WaitFor::None;
    }

    return WaitFor::None;
  }

  std::atomic<State> m_state = State::None;
};

} // namespace deskflow
