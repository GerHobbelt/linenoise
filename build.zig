const std = @import("std");

pub fn build(b: *std.Build) void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{
        .preferred_optimize_mode = .ReleaseSafe,
    });

    const linenoize = b.addStaticLibrary(.{
        .name = "linenoize",
        .target = target,
        .optimize = optimize,
    });
    linenoize.addIncludePath(".");
    linenoize.addCSourceFiles(&.{
        "linenoize.c",
    }, &.{
        "-std=c11",
        "-Wall",
        "-Wextra",
        "-Wpedantic",
    });
    linenoize.linkLibC();
    linenoize.install();
    linenoize.installHeadersDirectory("linenoize", "linenoize");
}
