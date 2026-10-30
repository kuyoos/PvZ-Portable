# SPDX-License-Identifier: LGPL-3.0-or-later
#
# Standalone project for the headless protocol tests. Keeping it separate from
# the game build means CI can run them without SDL, vcpkg or libopenmpt.
cmake_minimum_required(VERSION 3.12)
project(pvz_net_test LANGUAGES CXX)

add_executable(pvz_net_test
	net_protocol_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../src/PvzpLib/net/NetFrame.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../src/PvzpLib/net/NetJson.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../src/PvzpLib/net/NetMsg.cpp
)

target_include_directories(pvz_net_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_features(pvz_net_test PRIVATE cxx_std_20)

if(MSVC)
	target_compile_options(pvz_net_test PRIVATE /utf-8)
endif()

add_executable(pvz_lockstep_test
	net_lockstep_test.cpp
	${CMAKE_CURRENT_SOURCE_DIR}/../src/PvzpLib/net/Lockstep.cpp
)

target_include_directories(pvz_lockstep_test PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_features(pvz_lockstep_test PRIVATE cxx_std_20)

if(MSVC)
	target_compile_options(pvz_lockstep_test PRIVATE /utf-8)
endif()

enable_testing()
add_test(NAME net_protocol COMMAND pvz_net_test)
add_test(NAME net_lockstep COMMAND pvz_lockstep_test)
