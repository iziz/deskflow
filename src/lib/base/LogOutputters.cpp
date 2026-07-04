/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 - 2026 Deskflow Developers
 * SPDX-FileCopyrightText: (C) 2012 - 2016 Synergy App Ltd
 * SPDX-FileCopyrightText: (C) 2002 Chris Schoeneman
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "base/LogOutputters.h"
#include "arch/Arch.h"

#include <iostream>

#include <QFile>
#include <QString>

constexpr auto s_logFileSizeLimit = 1024 * 1024; //!< Max Log size before rotating (1Mb)
constexpr auto s_logBufferSize = 64 * 1024;      //!< Batch verbose writes to keep logging off the input hot path
constexpr auto s_logFlushIntervalMs = 250;       //!< Keep buffered diagnostics available to readers

//
// StopLogOutputter
//

void StopLogOutputter::open(const QString &)
{
  // do nothing
}

void StopLogOutputter::close()
{
  // do nothing
}

bool StopLogOutputter::write(LogLevel::Level, const QString &)
{
  return false;
}

//
// ConsoleLogOutputter
//

void ConsoleLogOutputter::open(const QString &title)
{
  // do nothing
}

void ConsoleLogOutputter::close()
{
  // do nothing
}

bool ConsoleLogOutputter::write(LogLevel::Level level, const QString &msg)
{
  using enum LogLevel::Level;
  if ((level >= Fatal) && (level <= Warning)) {
    std::cout.flush();
    std::cerr << qPrintable(msg) << '\n';
    std::cerr.flush();
  } else {
    std::cout << qPrintable(msg) << '\n';
    if (level != Verbose) {
      std::cout.flush();
    }
  }
  return true;
}

void ConsoleLogOutputter::flush() const
{
  std::cout.flush();
  std::cerr.flush();
}

//
// SystemLogOutputter
//

void SystemLogOutputter::open(const QString &title)
{
  ARCH->openLog(title);
}

void SystemLogOutputter::close()
{
  ARCH->closeLog();
}

bool SystemLogOutputter::write(LogLevel::Level level, const QString &msg)
{
  ARCH->writeLog(level, msg);
  return true;
}

//
// SystemLogger
//

SystemLogger::SystemLogger(const QString &title, bool blockConsole)
{
  // redirect log messages
  if (blockConsole) {
    m_stop = new StopLogOutputter; // NOSONAR - Adopted by `Log`
    CLOG->insert(m_stop);
  }
  m_syslog = new SystemLogOutputter; // NOSONAR - Adopted by `Log`
  m_syslog->open(title);
  CLOG->insert(m_syslog);
}

SystemLogger::~SystemLogger()
{
  CLOG->remove(m_syslog);
  delete m_syslog;
  if (m_stop != nullptr) {
    CLOG->remove(m_stop);
    delete m_stop;
  }
}

//
// FileLogOutputter
//

FileLogOutputter::FileLogOutputter(const QString &logFile)
{
  setLogFilename(logFile);
}

FileLogOutputter::~FileLogOutputter()
{
  close();
}

void FileLogOutputter::setLogFilename(const QString &logFile)
{
  assert(logFile != nullptr);
  close();
  m_fileName = logFile;
  m_file.setFileName(logFile);
}

bool FileLogOutputter::write(LogLevel::Level level, const QString &message)
{
  if (!openFile()) {
    return false;
  }

  auto entry = message.toUtf8();
  entry.append('\n');

  if (m_file.size() + m_buffer.size() + entry.size() > s_logFileSizeLimit &&
      (m_file.size() > 0 || !m_buffer.isEmpty())) {
    if (!flushBuffer() || !rotateFile()) {
      return false;
    }
  }

  m_buffer.append(entry);

  // Verbose input and protocol events can produce thousands of messages per
  // second. Batch those messages, but keep higher-priority logs immediately
  // visible and durable.
  if (level != LogLevel::Level::Verbose || m_buffer.size() >= s_logBufferSize ||
      m_flushTimer.elapsed() >= s_logFlushIntervalMs) {
    return flushBuffer();
  }

  return true;
}

bool FileLogOutputter::openFile()
{
  if (m_file.isOpen()) {
    return true;
  }

  const auto opened = m_file.open(QFile::WriteOnly | QFile::Append);
  if (opened && !m_flushTimer.isValid()) {
    m_flushTimer.start();
  }
  return opened;
}

bool FileLogOutputter::flushBuffer()
{
  if (m_buffer.isEmpty()) {
    return true;
  }
  if (!openFile()) {
    return false;
  }

  const auto bytesWritten = m_file.write(m_buffer);
  if (bytesWritten < 0) {
    return false;
  }
  m_buffer.remove(0, bytesWritten);

  const auto flushed = m_buffer.isEmpty() && m_file.flush();
  if (flushed) {
    m_flushTimer.restart();
  }
  return flushed;
}

bool FileLogOutputter::rotateFile()
{
  if (!m_buffer.isEmpty() || !openFile()) {
    return false;
  }

  m_file.close();
  const auto oldFile = QStringLiteral("%1.1").arg(m_fileName);
  QFile::remove(oldFile);
  if (!QFile::rename(m_fileName, oldFile)) {
    openFile();
    return false;
  }

  return openFile();
}

void FileLogOutputter::open(const QString &)
{
  openFile();
}

void FileLogOutputter::close()
{
  flushBuffer();
  m_file.close();
  m_flushTimer.invalidate();
}
