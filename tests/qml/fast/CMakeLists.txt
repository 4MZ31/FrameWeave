# SPDX-License-Identifier: GPL-3.0-or-later
# Copyright (C) 2026 DosukaSOL and the PushFPS contributors. Part of PushFPS, a modified version of Veyra.
#
# PushFPS: the interface tests' program (tests/qml/QuickSmokeTests.cpp) on its own, without the engine,
# for the qml-tests-fast workflow (Linux, a few minutes). The program needs nothing but Qt: the bridge
# it gives the tests is the QML object written in that file. The root CMakeLists.txt builds the same
# source for the Windows build, which is the one that counts.
cmake_minimum_required(VERSION 3.21)
project(pushfps_qml_tests_fast LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
find_package(Qt6 REQUIRED COMPONENTS Gui Qml Quick QuickTest Test)
add_executable(veyra_qml_quick_tests ../QuickSmokeTests.cpp)
target_compile_definitions(veyra_qml_quick_tests PRIVATE
  VEYRA_QML_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../../qml"
  QUICK_TEST_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../quick")
target_link_libraries(veyra_qml_quick_tests PRIVATE Qt6::Gui Qt6::Qml Qt6::Quick Qt6::QuickTest Qt6::Test)
