const std = @import("std");

const Build = std.Build;
const Module = Build.Module;
const Step = Build.Step;
const ResolvedTarget = Build.ResolvedTarget;
const OptimizeMode = std.builtin.OptimizeMode;


const cpp_examples = [_][]const u8{
    "midiprobe",
    "midiobserve",
    // Add other examples once proper cpp library build is fixed
};

const zig_examples = [_][]const u8{
    "zig_api",
};

pub fn build(b: *std.Build) !void {
    const target = b.standardTargetOptions(.{});
    const optimize = b.standardOptimizeOption(.{});

    const config = .{
        .target = target,
        .optimize = optimize,

        .no_coremidi = b.option(bool, "no_coremidi", "Disable CoreMidi back-end") orelse false,
        .no_winmm = b.option(bool, "no_winmm", "Disable WinMM back-end") orelse false,
        .no_winuwp = b.option(bool, "no_winuwp", "Disable UWP back-end") orelse false,
        .no_winmidi = b.option(bool, "no_winmidi", "Disable WinMIDI back-end") orelse false,

        .no_alsa = b.option(bool, "no_alsa", "Disable ALSA back-end") orelse false,
        .no_udev = b.option(bool, "no_udev", "Disable udev support for ALSA") orelse false,
        .no_jack = b.option(bool, "no_jack", "Disable JACK back-end") orelse false,
        .no_pipewire = b.option(bool, "no_pipewire", "Disable PipeWire back-end") orelse false,
        .no_network = b.option(bool, "no_network", "Disable Network back-end") orelse false,
        .no_keyboard = b.option(bool, "no_keyboard", "Disable Computer keyboard back-end") orelse false,

        .no_exports = b.option(bool, "no_exports", "Disable dynamic symbol exporting") orelse false,
        .no_boost = b.option(bool, "no_boost", "Do not use Boost if available") orelse false,
        .slim_message = b.option(usize, "slim_message", "Use a fixed-size message format"),
        .ni_midi2 = b.option(bool, "ni_midi2", "Enable compatibility with ni-midi2") orelse false,
        // .ci = b.option(bool, "ci", "To be enabled only in CI, some tests cannot run there. Also enables -Werror.") orelse false,
    };

    // const cpp_lib = addLibremidiCppLibrary(b, config);
    // b.installArtifact(cpp_lib);

    const c_obj = addLibremidiCObject(b, config);
    const libremidi = addLibremidiZigModule(b, c_obj, "libremidi", config);
    // _ = libremidi; // autofix

    // const lib = b.addLibrary(.{
    //     .name = "libremidi-zig",
    //     .root_module = libremidi,
    // });
    // _ = lib; // autofix
    // b.installArtifact(lib);

    addExamplesStep(b, libremidi, config);
}

fn addLibremidiZigModule(b: *std.Build, c_obj: *Build.Step.Compile, name: []const u8, config: anytype) *Build.Module {

    const translated_header = b.addTranslateC(.{
        .root_source_file = b.path("include/libremidi/libremidi-c.h"),
        .target = b.graph.host,
        .optimize = config.optimize,
    });
    translated_header.addIncludePath(b.path("include/"));

    const libremidi_c_mod = b.createModule(.{
        .root_source_file = translated_header.getOutput(),
        .target = config.target,
        .optimize = config.optimize,
    });
    libremidi_c_mod.addObject(c_obj);


    const libremidi_zig_mod = b.addModule(name, .{
        .root_source_file = b.path("bindings/zig/libremidi.zig"),
        .target = config.target,
        .optimize = config.optimize,
        .imports = &.{
            .{ .name = "libremidi-c", .module = libremidi_c_mod },
        },
    });

    return libremidi_zig_mod;
}

fn addLibremidiCObject(b: *std.Build, config: anytype) *Build.Step.Compile {

    const c_obj = b.addObject(.{
        .name = "libremidi-c",
        .root_module = b.createModule(.{
            .target = config.target,
            .optimize = config.optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    c_obj.root_module.addIncludePath(b.path("include/"));
    c_obj.root_module.addCSourceFiles(.{
        .files = &.{
            "include/libremidi/libremidi-c.cpp",
        },
        .flags = &.{ "-std=c++20" },
    });
    addCMacroNoValue(c_obj.root_module, "LIBREMIDI_HEADER_ONLY");
    c_obj.root_module.linkSystemLibrary("pthread", .{ .preferred_link_mode = .static });


    addLibremidiConfig(b, c_obj.root_module, config);

    return c_obj;
}

// TODO: fix this
fn addLibremidiCppLibrary(b: *std.Build, config: anytype) *Build.Step.Compile {

    const cpp_lib = b.addStaticLibrary(.{
        .name = "libremidi",
        .root_module = b.createModule(.{
            .target = config.target,
            .optimize = config.optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    addCMacroNoValue(cpp_lib.root_module, "LIBREMIDI_HEADER_ONLY");
    cpp_lib.root_module.addIncludePath(b.path("include/"));
    cpp_lib.root_module.addCSourceFiles(.{
        .files = &.{
            "include/libremidi/libremidi.hpp",
        },
        .flags = &.{ "-std=c++20" },
    });

    addLibremidiConfig(b, cpp_lib.root_module, config);

    return cpp_lib;
}

fn addLibremidiConfig(b: *std.Build, module: *Build.Module, config: anytype) void {

    const boost = addBoostConfig(b, module, config);
    addSlimMessageConfig(b, module, boost, config);
    addExportsConfig(b, module , config);
    addNiMidi2Config(b, module, config); // Broken
    addEmscriptenConfig(b, module, config); // Broken
    addWin32Config(b, module, config); // Unimplemented
    addCoremidiConfig(b, module, config); // Unimplemented
    addAlsaConfig(b, module, config);
    addJackConfig(b, module, config);
    addPipewireConfig(b, module, config); // Broken
    addKeyboardConfig(b, module, config);
    addNetworkConfig(b, module, boost, config); // Broken
}

fn addCMacroNoValue(module: *Build.Module, macro: []const u8) void {
    module.addCMacro(macro, "");
}

fn addCMacroNumeric(module: *Build.Module, macro: []const u8, value: anytype) void {

    var should_hold_any_int: [50]u8 = undefined;
    module.addCMacro(macro, std.fmt.bufPrint(&should_hold_any_int, "{d}", .{value}) catch @panic("MacroTooBig"));
}

fn addBoostConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) ?*Build.Module {
    if (config.no_boost) {
        addCMacroNoValue(libremidi_c, "LIBREMIDI_NO_BOOST");
        return null;
    }

    addCMacroNoValue(libremidi_c, "LIBREMIDI_USE_BOOST");

    const boost = b.dependency("boost", .{ .target = config.target, .optimize = config.optimize });
    const boost_artifact = boost.artifact("boost");

    for (boost_artifact.root_module.include_dirs.items) |include_dir|
        libremidi_c.include_dirs.append(b.allocator, include_dir) catch @panic("OOM");

    libremidi_c.linkLibrary(boost_artifact);

    return boost_artifact.root_module;
}

fn addSlimMessageConfig(b: *std.Build, libremidi_c: *Build.Module, boost: ?*Build.Module, config: anytype) void {
    _ = b;

    if (boost) |_| if (config.slim_message) |size|
        addCMacroNumeric(libremidi_c, "SLIM_MESSAGE", size);
}

fn addExportsConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b;

    if (!config.no_exports) addCMacroNoValue(libremidi_c, "LIBREMIDI_EXPORTS");
}

// TODO: fix
fn addNiMidi2Config(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    if (!config.ni_midi2) return;

    if (true) return; // disable until fixed

    const nimidi2_dep = b.dependency("ni_midi2", .{});

    const nimidi2_lib = b.addStaticLibrary(.{
        .name = "ni-midi2",
        .root_module = b.createModule(.{
            .target = config.target,
            .optimize = config.optimize,
        })
    });

    nimidi2_lib.root_module.addIncludePath(nimidi2_dep.path("inc/"));
    nimidi2_lib.root_module.addCSourceFiles(.{
        .root = nimidi2_dep.path("src/"),
        .files = &.{
            "capability_inquiry.cpp",
            "jitter_reduction_timestamps.cpp",
            "midi1_byte_stream.cpp",
            "sysex.cpp",
            "sysex_collector.cpp",
            "universal_packet.cpp",
            "universal_sysex.cpp",
        },
        .flags = &.{ "-std=c++20" },
    });
    b.installArtifact(nimidi2_lib);
    // const tkt = b.addInstallArtifact(nimidi2_lib, .{ .dest_dir = .{ .override = .{ .custom = "obj/" } } });
    // b.getInstallStep().dependOn(&tkt.step);

    // this compiles but I suspect it will silently fail since it is still not linked to ni-midi2
    libremidi_c.addCMacro("LIBREMIDI_USE_NI_MIDI2", "1");
    libremidi_c.addIncludePath(nimidi2_dep.path("inc/"));
}

// TODO: fix
fn addEmscriptenConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    if (config.target.result.os.tag != .emscripten) return;

    _ = b;

    addCMacroNoValue(libremidi_c, "LIBREMIDI_EMSCRIPTEN");
}

// TODO: Implement
fn addWin32Config(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b;
    _ = libremidi_c;
    _ = config;
}

// TODO: Implement
fn addCoremidiConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b;
    _ = libremidi_c;
    _ = config;
}

// TODO: Could it work on some other OSes?
fn addAlsaConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b; // to keep the signatures the same between all add*Config() functions
    if ((config.no_alsa) or (config.target.result.os.tag != .linux)) return;

    addCMacroNoValue(libremidi_c, "LIBREMIDI_ALSA");
    libremidi_c.linkSystemLibrary("asound", .{ .preferred_link_mode = .dynamic });

    if (config.no_udev) return;

    // Libremidi code needs the "1" value, change to that if/once fixed
    // addCMacroNoValue(libremidi_c, "LIBREMIDI_HAS_UDEV");
    libremidi_c.addCMacro("LIBREMIDI_HAS_UDEV", "1");
}

// TODO: fix weakjack, make work on other OSes?
fn addJackConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b;
    if ((config.no_jack) or (config.target.result.os.tag != .linux)) return;

    addCMacroNoValue(libremidi_c, "LIBREMIDI_JACK");
    // libremidi_c.addCMacro("LIBREMIDI_WEAKJACK", "1");

    libremidi_c.linkSystemLibrary("jack", .{});
}

// TODO: fix
fn addPipewireConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    if ((config.no_pipewire) or (config.target.result.os.tag != .linux)) return;

    if (true) return; // disable until fixed

    addCMacroNoValue(libremidi_c, "LIBREMIDI_PIPEWIRE");

    const rwq_path = b.dependency("readerwriterqueue", .{}).path("");
    libremidi_c.addIncludePath(rwq_path);

    // libremidi_c.addSystemIncludePath(.{ .cwd_relative = "/usr/include/pipewire-0.3/" });
    // libremidi_c.addSystemIncludePath(.{ .cwd_relative = "/usr/include/spa-0.2/" });

    libremidi_c.linkSystemLibrary("pipewire-0.3", .{});
}

fn addKeyboardConfig(b: *std.Build, libremidi_c: *Build.Module, config: anytype) void {
    _ = b;

    if (!config.no_keyboard) addCMacroNoValue(libremidi_c, "LIBREMIDI_KEYBOARD");
}

// TODO: fix
fn addNetworkConfig(b: *std.Build, libremidi_c: *Build.Module, maybe_boost: ?*Build.Module, config: anytype) void {
    _ = b; // to keep the signatures the same between all add*Config() functions

    if (config.no_network) return;
    const boost = maybe_boost orelse return;

    if (true) return; // disable until fixed

    addCMacroNoValue(libremidi_c, "LIBREMIDI_NETWORK");

    if (config.target.result.os.tag == .macos)
        if (!(config.target.result.os.isAtLeast(.macos, .{ .major = 15, .minor = 0, .patch = 0 }) orelse false))
            boost.addCMacro("BOOST_ASIO_DISABLE_STD_ALIGNED_ALLOC", "1");


    boost.addCMacro("BOOST_ASIO_HAS_STD_INVOKE_RESULT", "1");

    // something something win32 implement
}

fn addExamplesStep(b: *std.Build, libremidi: *Module, config: anytype) void {

    const step = b.step("examples", "Build the examples");

    inline for (cpp_examples) |name| {

        const example_exe = addCppExample(b, name, config);
        const artifact = b.addInstallArtifact(example_exe, .{});

        step.dependOn(&artifact.step);
    }

    inline for (zig_examples) |name| {

        const example_exe = addZigExample(b, libremidi, name, config);
        const artifact = b.addInstallArtifact(example_exe, .{});

        step.dependOn(&artifact.step);
    }
}

// TODO: improve/fix by figuring out how to compile liblibremidi.a properly
fn addCppExample(b: *std.Build, name: []const u8, config: anytype) *Build.Step.Compile {

    var buf: [512]u8 = undefined;

    const example_exe = b.addExecutable(.{
        .name = name,
        .root_module = b.createModule(.{
            .target = config.target,
            .optimize = config.optimize,
            .link_libc = true,
            .link_libcpp = true,
        }),
    });

    addCMacroNoValue(example_exe.root_module, "LIBREMIDI_HEADER_ONLY");
    example_exe.root_module.addIncludePath(b.path("include/"));
    example_exe.root_module.addCSourceFiles(.{
        .files = &.{
            std.fmt.bufPrint(&buf, "examples/{s}.cpp", .{name}) catch @panic("BufferTooSmall"),
        },
        .flags = &.{ "-std=c++20" },
    });

    addLibremidiConfig(b, example_exe.root_module, config);


    return example_exe;
}

fn addZigExample(b: *std.Build, libremidi: *Build.Module, name: []const u8, config: anytype) *Build.Step.Compile {

    var buf: [512]u8 = undefined;

    const zig_exe = b.addExecutable(.{
        .name = name,
        .root_module = b.createModule(.{
            .root_source_file = b.path(std.fmt.bufPrint(&buf, "bindings/zig/examples/{s}.zig", .{name}) catch @panic("BufferTooSmall")),
            .target = config.target,
            .optimize = config.optimize,
            .imports = &.{
                .{ .name = "libremidi", .module = libremidi },
            },
        }),
    });


    return zig_exe;
}
