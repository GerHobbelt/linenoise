const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseSafe,
    });

    const linenoise = b.addStaticLibrary(.{
        .name = "linenoise",
        .target = target,
        .optimize = optimize,
    });
    linenoise.addIncludePath(".");
    linenoise.addCSourceFiles(&.{
        "linenoise.c",
    }, &.{
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
    });
    linenoise.linkLibC();
    linenoise.install();
    linenoise.installHeadersDirectory("linenoise", "linenoise");
}
