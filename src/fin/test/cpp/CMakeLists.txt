# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2026, The OpenROAD Authors

add_executable(FillUtillTest FillUtillTest.cpp)
target_include_directories(FillUtillTest PRIVATE ../../src)
target_link_libraries(FillUtillTest
  fin
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(FillUtillTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test FillUtillTest)

add_executable(FixedDissectionLpTest FixedDissectionLpTest.cpp)
target_include_directories(FixedDissectionLpTest PRIVATE ../../src)
target_link_libraries(FixedDissectionLpTest
  fin
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(FixedDissectionLpTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test FixedDissectionLpTest)

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
