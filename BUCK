# BUILD FILE SYNTAX: SKYLARK
load("//nodist:glob_defs.bzl", "subdir_glob")

cxx_library(
    name = "linenoise-ng",
    exported_headers = subdir_glob([
        ("include", "linenoise.h"),
    ]),
    headers=[
        "src/ConvertUTF.h",
    ],
    srcs = [
        "src/linenoise.cpp",
        "src/wcwidth.cpp",
        ("src/ConvertUTF.cpp", [
            "-Wno-keyword-macro"
        ]),
    ],
    visibility = [
        "PUBLIC",
    ],
)
