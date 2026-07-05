/*
 * Deskflow -- mouse and keyboard sharing utility
 * SPDX-FileCopyrightText: (C) 2025 Chris Rizzitello <sithlord48@gmail.com>
 * SPDX-FileCopyrightText: (C) 2024 Synergy App Ltd
 * SPDX-License-Identifier: GPL-2.0-only WITH LicenseRef-OpenSSL-Exception
 */

#include "LogTests.h"
#include "base/LogOutputters.h"
#include <clocale>
#include <iostream>
#include <sstream>

#include <QFile>
#include <QTemporaryDir>

#define LEVEL_PRINT "%z\057"
#define LEVEL_ERR "%z\061"
#define LEVEL_INFO "%z\063"
#define LEVEL_VERBOSE "%z\065"

QString sanitizeBuffer(const std::stringstream &in)
{
  static QRegularExpression timestampRegex("\\[\\S+\\] ");
  QString rtn = QString::fromStdString(in.str()).simplified();
  rtn.remove(timestampRegex);
  return rtn;
}

void LogTests::initTestCase()
{
  std::setlocale(LC_NUMERIC, "C");
  m_log.setFilter(LogLevel::Level::Debug);
}

void LogTests::printWithErrorValidOutput()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message");
}

void LogTests::printTestPrintLevel()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_PRINT "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "test message");
}

void LogTests::printTestWithArgs()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_INFO "test %s", "IamARG");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test IamARG");
}

void LogTests::printTestLogString()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  auto longString = QString(10000, 'a');
  m_log.print(nullptr, 0, LEVEL_INFO "%s", qPrintable(longString));

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString("INFO: %1").arg(longString));
}

void LogTests::printLevelToHigh()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(CLOG_VERBOSE "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, QString{});
}

void LogTests::printVerboseAboveConsoleMaxLevel()
{
  m_log.setFilter(LogLevel::Level::Verbose);
  m_log.setConsoleMaxLevel(LogLevel::Level::Debug);

  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print(nullptr, 0, LEVEL_VERBOSE "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);
  m_log.setConsoleMaxLevel(LogLevel::Level::Verbose);
  m_log.setFilter(LogLevel::Level::Debug);

  QCOMPARE(string, QString{});
}

void LogTests::printInfoWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cout.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_INFO "test message");

  auto string = sanitizeBuffer(buffer);
  std::cout.rdbuf(old);

  QCOMPARE(string, "INFO: test message test file:123");
}

void LogTests::printErrWithFileAndLine()
{
  std::stringstream buffer;
  std::streambuf *old = std::cerr.rdbuf(buffer.rdbuf());

  m_log.print("test file", 123, LEVEL_ERR "test message");

  auto string = sanitizeBuffer(buffer);
  std::cerr.rdbuf(old);

  QCOMPARE(string, "ERROR: test message test file:123");
}

void LogTests::fileOutputter_flushesBufferedVerboseMessages()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("deskflow.log"));

  {
    FileLogOutputter outputter(path);
    outputter.open({});
    QVERIFY(outputter.write(LogLevel::Level::Verbose, QStringLiteral("verbose message")));
  }

  QFile file(path);
  QVERIFY(file.open(QFile::ReadOnly));
  QCOMPARE(file.readAll(), QByteArray("verbose message\n"));
}

void LogTests::fileOutputter_rotatesWithoutDeletingCurrentLog()
{
  QTemporaryDir dir;
  QVERIFY(dir.isValid());
  const auto path = dir.filePath(QStringLiteral("deskflow.log"));

  QFile existing(path);
  QVERIFY(existing.open(QFile::WriteOnly));
  QCOMPARE(existing.write(QByteArray(1024 * 1024, 'x')), 1024 * 1024);
  existing.close();

  {
    FileLogOutputter outputter(path);
    outputter.open({});
    QVERIFY(outputter.write(LogLevel::Level::Info, QStringLiteral("current message")));
  }

  QFile rotated(path + QStringLiteral(".1"));
  QVERIFY(rotated.open(QFile::ReadOnly));
  QCOMPARE(rotated.size(), 1024 * 1024);

  QFile current(path);
  QVERIFY(current.open(QFile::ReadOnly));
  QCOMPARE(current.readAll(), QByteArray("current message\n"));
}

QTEST_MAIN(LogTests)
