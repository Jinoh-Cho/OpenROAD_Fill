load("@rules_cc//cc:cc_test.bzl", "cc_test")
cc_test(
    name = "tile_free_areas_test",
    srcs = [
        "TileFreeAreasTest.cpp",
        "InitialDensityTest.cpp",
        "CandidateOptimizationTest.cpp",
        "CandidateReference.cpp",
        "CandidateReference.h",
    ],
    includes = ["../../src"],
    copts = ["-fopenmp"],
    features = ["-layering_check"],
    deps = [
        "//src/fin:fin",
        "//src/gui:gui_stub",
        "@boost.polygon",
        "@openmp",
        "@googletest//:gtest_main",
    ],
)
