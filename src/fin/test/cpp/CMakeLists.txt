# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

add_executable(MLFixedDissectionLpTest MLFixedDissectionLpTest.cpp)
target_include_directories(MLFixedDissectionLpTest PRIVATE ../../src)
target_link_libraries(MLFixedDissectionLpTest
  fin
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(MLFixedDissectionLpTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test MLFixedDissectionLpTest)
