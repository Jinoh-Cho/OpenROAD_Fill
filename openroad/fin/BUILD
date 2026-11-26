# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2025, The OpenROAD Authors

load("@rules_cc//cc:cc_library.bzl", "cc_library")
load("@rules_cc//cc:cc_test.bzl", "cc_test")
load("//bazel:python_wrap_cc.bzl", "python_wrap_cc", "python_wrap_library")
load("//bazel:tcl_encode_or.bzl", "tcl_encode")
load("//bazel:tcl_wrap_cc.bzl", "tcl_wrap_cc")
load("//test:regression.bzl", "messages_txt")

package(
    default_visibility = ["//:__subpackages__"],
    features = ["layering_check"],
)

cc_library(
    name = "fin",
    srcs = [
        "src/DensityFill.cpp",
        "src/DensityFill.h",
        "src/FillCheck.cpp",
        "src/FillCheck.h",
        "src/FillCommon.cpp",
        "src/FillCommon.h",
        "src/Finale.cpp",
        "src/LPFill.cpp",
        "src/LPFill.h",
        "src/MakeFinale.cpp",
        "src/graphics.cpp",
        "src/graphics.h",
        "src/polygon.h",
        ":swig",
        ":tcl",
    ],
    hdrs = [
        "include/fin/Finale.h",
        "include/fin/MakeFinale.h",
    ],
    copts = ["-fopenmp"],
    includes = [
        "include",
    ],
    deps = [
        "//:ord",
        "//openroad/odb/src/db",
        "//openroad/utl",
        "//openroad/web:core",
        "@boost.json",
        "@boost.lexical_cast",
        "@boost.polygon",
        "@boost.property_tree",
        "@openmp",
        "@or-tools//ortools/linear_solver",
        "@or-tools//ortools/linear_solver:linear_solver_base",
        "@tcl_lang//:tcl",
    ],
)

tcl_encode(
    name = "tcl",
    srcs = [
        "src/finale.tcl",
    ],
    char_array_name = "fin_tcl_inits",
    namespace = "fin",
)

tcl_wrap_cc(
    name = "swig",
    srcs = [
        "src/finale.i",
        "//:error_swig",
    ],
    module = "fin",
    namespace_prefix = "fin",
    root_swig_src = "src/finale.i",
    swig_includes = [
        "src",
    ],
    deps = [
        "//openroad/odb:swig",
    ],
)

python_wrap_cc(
    name = "swig-py",
    srcs = [
        "include/fin/Finale.h",
        "src/finale-py.i",
        "//:error_swig-py",
    ],
    module = "fin_py",
    root_swig_src = "src/finale-py.i",
    swig_includes = [
        "include",
    ],
    deps = [
        "//openroad/odb:swig-py",
    ],
)

filegroup(
    name = "doc_files",
    srcs = [
        "README.md",
        "Window and Tile Definitions.png",
        "src/finale.tcl",
    ],
    visibility = ["//openroad/fin/test:__pkg__"],
)

messages_txt(
    visibility = [
        "//docs:__pkg__",
        "//openroad/fin/test:__pkg__",
    ],
)

python_wrap_library(
    name = "py_wrap",
    includes = ["src"],
    deps = [":fin"],
)

cc_test(
    name = "fill_grid_test",
    srcs = [
        "src/FillCheck.h",
        "src/polygon.h",
        "//openroad/fin/test:FillGridTest.cpp",
    ],
    deps = [
        ":fin",
        "//openroad/odb/src/db",
        "//openroad/utl",
        "@boost.json",
        "@boost.polygon",
        "@googletest//:gtest",
        "@googletest//:gtest_main",
    ],
)
