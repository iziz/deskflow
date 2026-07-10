# SPDX-FileCopyrightText: (C) 2026 Deskflow Developers
# SPDX-License-Identifier: MIT

create_test(
  NAME ServerKeyTranslatorTests
  DEPENDS client
  LIBS base
  SOURCE ServerKeyTranslatorTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/client"
)

create_test(
  NAME ClientClipboardStateTests
  DEPENDS client
  LIBS base
  SOURCE ClientClipboardStateTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/client"
)

create_test(
  NAME ClientInfoUpdateStateTests
  DEPENDS client
  LIBS base
  SOURCE ClientInfoUpdateStateTests.cpp
  WORKING_DIRECTORY "${CMAKE_BINARY_DIR}/src/lib/client"
)
