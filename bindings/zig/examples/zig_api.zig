const std = @import("std");
const lm = @import("libremidi");


const EnumeratedPorts = extern struct {
    in_ports: [256]?*lm.midi.In.Port = @splat(null),
    out_ports: [256]?*lm.midi.Out.Port = @splat(null),
    in_port_count: usize = 0,
    out_port_count: usize = 0,
};

pub fn main() !void {

    std.debug.print("Hello from libremidi Zig(ified) API example!\n", .{});
    std.debug.print("libremidi version: {s}\n\n", .{ lm.getVersion() });

    var e: EnumeratedPorts = .{};

    const observer: *lm.Observer = try .init(&.{
        .track_hardware = true,
        .track_virtual = true,
        .track_any = true,
        .input_added = .{
            .context = &e,
            .callback = on_input_port_found,
        },
        .output_added = .{
            .context = &e,
            .callback = on_output_port_found,
        },
    }, &.{
        .conf_type = .observer,
        .api = .alsa_seq,
    });
    defer free_observer(observer, &e);

    try enumerate_ports(observer, &e);

    const midi_in: *lm.midi.In = try .init(&.{
        .version = .midi1,
        .port = .{ .input = e.in_ports[0] },
        .msg_callback = .{ .on_midi1_message = .{ .callback = on_midi1_message } },
    }, &.{
        .conf_type = .input,
        .api = .alsa_seq,
    });
    defer midi_in.free();

    const midi_out: *lm.midi.Out = try .init(&.{
        .version = .midi1,
        .virtual_port = true,
        .port_name = "my-app",
    }, &.{
        .conf_type = .output,
        .api = .alsa_seq,
    });
    defer midi_out.free();


    for (0..99) |_| std.time.sleep(1e9); // sleep 1s, 100 times
}

fn free_observer(observer: *lm.Observer, e: *EnumeratedPorts) void {

    for (e.in_ports) |maybe_port| if (maybe_port) |port| port.free();
    for (e.out_ports) |maybe_port| if (maybe_port) |port| port.free();

    observer.free();
}

export fn on_input_port_found(ctx: ?*anyopaque, port: *lm.midi.In.Port) callconv(.C) void {

    std.debug.print("input: {s}\n", .{ port.getName() catch "" });

    var e: *EnumeratedPorts = @ptrCast(@alignCast(ctx));
    e.in_ports[e.in_port_count] = port.clone() catch null;
    e.in_port_count += 1;
}

export fn on_output_port_found(ctx: ?*anyopaque, port: *lm.midi.Out.Port) callconv(.C) void {

    std.debug.print("output: {s}\n", .{ port.getName() catch "" });

    var e: *EnumeratedPorts = @ptrCast(@alignCast(ctx));
    e.out_ports[e.out_port_count] = port.clone() catch null;
    e.out_port_count += 1;
}

fn on_midi1_message(ctx: ?*anyopaque, ts: lm.Timestamp, msg: [*]const lm.midi.v1.Symbol, len: usize) callconv(.C) void {
    _ = ctx;
    _ = ts;
    _ = len;

    std.debug.print("0x{x:02} 0x{x:02} 0x{x:02}\n", .{ msg[0], msg[1], msg[2] });
}

fn on_midi2_message(ctx: ?*anyopaque, ts: lm.Timestamp, msg: [*]const lm.midi.v2.Symbol, len: usize) callconv(.C) void {
    _ = ctx;
    _ = ts;
    _ = len;

    std.debug.print("0x{x:02} 0x{x:02} 0x{x:02}\n", .{ msg[0], msg[1], msg[2] });
}

fn enumerate_ports(observer: *lm.Observer, e: *EnumeratedPorts) !void {
    try observer.enumerateInputPorts(e, on_input_port_found);
    try observer.enumerateOutputPorts(e, on_output_port_found);
}
