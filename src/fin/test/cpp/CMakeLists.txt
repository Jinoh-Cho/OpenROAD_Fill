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
  ortools::ortools
  Boost::boost
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(FixedDissectionLpTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test FixedDissectionLpTest)

add_executable(MetalCollectionTest ../metal_collection/MetalCollectionTest.cpp)
target_include_directories(MetalCollectionTest PRIVATE ../../src)
target_link_libraries(MetalCollectionTest fin GTest::gtest GTest::gtest_main)
gtest_discover_tests(MetalCollectionTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test MetalCollectionTest)

add_executable(TileFreeAreasTest ../tile_free_areas/TileFreeAreasTest.cpp
  ../tile_free_areas/InitialDensityTest.cpp
  ../tile_free_areas/CandidateOptimizationTest.cpp
  ../tile_free_areas/CandidateReference.cpp)
target_include_directories(TileFreeAreasTest PRIVATE ../../src)
target_link_libraries(TileFreeAreasTest
  fin
  gui
  OpenMP::OpenMP_CXX
  GTest::gtest
  GTest::gtest_main
)
gtest_discover_tests(TileFreeAreasTest
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../..
)
add_dependencies(build_and_test TileFreeAreasTest)
