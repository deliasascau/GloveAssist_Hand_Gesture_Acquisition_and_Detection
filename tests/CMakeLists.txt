# SPDX-License-Identifier: Apache-2.0
# GloveAssist Unit Tests — Ztest
#
# Tests cover transport-agnostic modules only (no hardware drivers):
#   - frame_protocol.c : frame_build(), frame_build_hmac(), frame_validate_hmac()
#
# Build: west build -b native_sim tests/

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(gloveassist_tests LANGUAGES C)

target_include_directories(app PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/../application/common/include
)

target_sources(app PRIVATE
    unit_tests.c
    ../application/common/src/frame_protocol.c
)
