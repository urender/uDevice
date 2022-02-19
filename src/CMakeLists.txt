cmake_minimum_required(VERSION 3.13)
include(CheckFunctionExists)
include(CheckSymbolExists)

PROJECT(uc_device C)
ADD_DEFINITIONS(-Os -Wall -Werror --std=gnu99 -ffunction-sections -D_GNU_SOURCE)

IF(CMAKE_C_COMPILER_VERSION VERSION_GREATER 6)
	ADD_DEFINITIONS(-Wextra -Werror=implicit-function-declaration)
	ADD_DEFINITIONS(-Wformat -Werror=format-security -Werror=format-nonliteral)
ENDIF()
ADD_DEFINITIONS(-Wmissing-declarations -Wno-error=unused-variable -Wno-unused-parameter)

IF(NOT APPLE)
  SET(CMAKE_SHARED_LIBRARY_LINK_C_FLAGS "-Wl,--gc-sections")
ENDIF()

FIND_LIBRARY(json NAMES json-c json)

IF(DEBUG)
  ADD_DEFINITIONS(-DDEBUG -g3 -O0)
ELSE()
  ADD_DEFINITIONS(-DNDEBUG)
ENDIF()

IF(NOT LEGACY_SUPPORT)
  ADD_DEFINITIONS(-DNO_LEGACY)
ENDIF()

SET(UCODE_SOURCES ucode.c websocket.c)
ADD_LIBRARY(uc_device SHARED ${UCODE_SOURCES})
FIND_LIBRARY(websockets NAMES websockets)
SET_TARGET_PROPERTIES(uc_device PROPERTIES OUTPUT_NAME uDevice PREFIX "")
TARGET_LINK_LIBRARIES(uc_device ${websockets})

INSTALL(TARGETS uc_device LIBRARY DESTINATION lib)
