load("@rules_cc//cc:cc_test.bzl", "cc_test")

cc_test(
    name = "metal_collection_test",
    srcs = [
        "MetalCollectionTest.cpp",
        "//src/fin:src/FillGeometry.cpp",
        "//src/fin:src/FillGeometry.h",
        "//src/fin:src/polygon.h",
    ],
    includes = ["../../src"],
    copts = ["-fopenmp"],
    features = ["-layering_check"],
    deps = [
        "//src/odb/src/db",
        "@boost.polygon",
        "@openmp",
        "@googletest//:gtest_main",
    ],
)
