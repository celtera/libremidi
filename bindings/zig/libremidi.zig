const std = @import("std");
const c = @import("libremidi-c");

const E = std.c.E;
fn errnoFromInt(rc: anytype) E {
    return .init(@bitCast(@as(isize, rc)));
}


extern fn libremidi_get_version() [*:0]const u8;
pub const getVersion = libremidi_get_version;

pub const Api = enum(c.libremidi_api) {
    unspecified = c.UNSPECIFIED,

    coremidi = c.COREMIDI,
    alsa_seq = c.ALSA_SEQ,
    alsa_raw = c.ALSA_RAW,
    jack_midi = c.JACK_MIDI,
    windows_mm = c.WINDOWS_MM,
    windows_uwp = c.WINDOWS_UWP,
    webmidi = c.WEBMIDI,
    pipewire = c.PIPEWIRE,
    keyboard = c.KEYBOARD,
    network = c.NETWORK,

    alsa_raw_ump = c.ALSA_RAW_UMP,
    alsa_seq_ump = c.ALSA_SEQ_UMP,
    coremidi_ump = c.COREMIDI_UMP,
    windows_midi_services = c.WINDOWS_MIDI_SERVICES,
    keyboard_ump = c.KEYBOARD_UMP,
    network_ump = c.NETWORK_UMP,
    jack_ump = c.JACK_UMP,
    pipewire_ump = c.PIPEWIRE_UMP,

    dummy = c.DUMMY,


    extern fn libremidi_api_identifier(self: Api) [*:0]const u8;
    pub const getId = libremidi_api_identifier;

    extern fn libremidi_api_display_name(self: Api) [*:0]const u8;
    pub const getDisplayName = libremidi_api_display_name;

    extern fn libremidi_get_compiled_api_by_identifier(name: [*:0]const u8) Api;
    pub const getById = libremidi_get_compiled_api_by_identifier;

    pub const Config = extern struct {

        api: Api = .unspecified,

        conf_type: enum(@FieldType(c.libremidi_api_configuration, "configuration_type")) {
            observer = c.Observer,
            input = c.Input,
            output = c.Output,
        } = .observer,

        data: ?*anyopaque = null,
    };

    fn Callback(comptime P: type) type {
        return (?*const fn(ctx: P, api: Api) callconv(.C) void);
    }
};

pub const Timestamp = extern struct {
    inner:  c.libremidi_timestamp = 0,


    pub const Mode = enum(c.enum_libremidi_timestamp_mode) {
        no_timestamp = c.NoTimestamp,

        relative = c.Relative,
        absolute = c.Absolute,
        system_monotonic = c.SystemMonotonic,
        audio_frame = c.AudioFrame,
        custom = c.Custom,
    };

    fn Callback(comptime P: type) type {
        return extern struct {
            context: P = null,
            callback: (?*const fn(ctx: P, ts: Timestamp) callconv(.C) Timestamp) = null,
        };
    }
};

pub const observer = struct {
    const Ctx = ?*anyopaque;

    pub const Handle = extern struct {
        const Self = @This();
        const Opaque = c.libremidi_midi_observer_handle;

        ptr: ?*const Opaque = null,


        extern fn libremidi_midi_observer_new(conf: ?*const Config, api: ?*const Api.Config, out: *Self) c_int;
        pub fn init(conf: ?*const Config, api: ?*const Api.Config) !Self {

            var handle: Self = .{};
            switch (errnoFromInt(libremidi_midi_observer_new(conf, api, &handle))) {
                .SUCCESS => return handle,
                .INVAL => return error.InvalidArgument,
                .IO => return error.InputOutput,
                else => unreachable,
            }
        }

        extern fn libremidi_midi_observer_enumerate_input_ports(self: Self, context: Ctx, cb: midi.in.port.Callback(Ctx)) c_int;
        pub fn enumerateInputPorts(self: Self, context: Ctx, cb: midi.in.port.Callback(Ctx)) !void {
            switch (errnoFromInt(libremidi_midi_observer_enumerate_input_ports(self, context, cb))) {
                .SUCCESS => return,
                .INVAL => return error.InvalidArgument,
                else => unreachable,
            }
        }

        extern fn libremidi_midi_observer_enumerate_output_ports(self: Self, context: Ctx, cb: midi.out.port.Callback(Ctx)) c_int;
        pub fn enumerateOutputPorts(self: Self, context: Ctx, cb: midi.out.port.Callback(Ctx)) !void {
            switch (errnoFromInt(libremidi_midi_observer_enumerate_output_ports(self, context, cb))) {
                .SUCCESS => return,
                .INVAL => return error.InvalidArgument,
                else => unreachable,
            }
        }

        extern fn libremidi_midi_observer_free(self: Self) c_int;
        pub fn free(self: Self) void {
            switch (libremidi_midi_observer_free(self)) {
                0 => return,
                else => unreachable,
            }
        }
    };

    pub const Config = extern struct {

        on_error: ErrCallback(Ctx) = .{ .context = null, .callback = null },
        on_warning: ErrCallback(Ctx) = .{ .context = null, .callback = null },
        input_added: InputCallback(Ctx) = .{ .context = null, .callback = null },
        input_removed: InputCallback(Ctx) = .{ .context = null, .callback = null },
        output_added: OutputCallback(Ctx) = .{ .context = null, .callback = null },
        output_removed: OutputCallback(Ctx) = .{ .context = null, .callback = null },
        track_hardware: bool = false,
        track_virtual: bool = false,
        track_any: bool = false,
        notify_in_constructor: bool = false,


        fn ErrCallback(comptime P: type) type {
            return extern struct {
                const Loc = ?*const anyopaque;

                context: P = null,
                callback: (?*const fn(ctx: P, err: [*:0]const u8, err_len: usize, source_location: Loc) callconv(.C) void) = null,
            };
        }

        fn InputCallback(comptime P: type) type {
            return extern struct {
                context: P = null,
                callback: midi.in.port.Callback(P) = null,
            };
        }

        fn OutputCallback(comptime P: type) type {
            return extern struct {
                context: P = null,
                callback: midi.out.port.Callback(P) = null,
            };
        }
    };
};

pub const midi = struct {

    pub const Config = extern struct {
        const Ctx = ?*anyopaque;

        version: enum(@FieldType(c.libremidi_midi_configuration, "version")) {
            none = 0,

            midi1 = c.MIDI1, midi1_raw = c.MIDI1_RAW,
            midi2 = c.MIDI2, midi2_raw = c.MIDI2_RAW,
        } = .none,

        port: extern union {
            input: midi.in.port.Handle,
            output: midi.out.port.Handle,
        } = .{ .input = .{ .ptr = null } },

        msg_callback: extern union {
            on_midi1_message: v1.Callback(Ctx),
            on_midi1_raw_data: v1.Callback(Ctx),
            on_midi2_message: v2.Callback(Ctx),
            on_midi2_raw_data: v2.Callback(Ctx),
        } = .{ .on_midi1_message = .{ .context = null, .callback = null } },

        ts_callback: Timestamp.Callback(Ctx) = .{ .context = null, .callback = null },
        on_error: ErrCallback(Ctx) = .{ .context = null, .callback = null },
        on_warning: ErrCallback(Ctx) = .{ .context = null, .callback = null },
        port_name: ?[*:0]const u8 = null,
        virtual_port: bool = false,
        ignore_sysex: bool = false,
        ignore_timing: bool = false,
        ignore_sensing: bool = false,
        timestamps: Timestamp.Mode = .no_timestamp,


        fn ErrCallback(comptime P: type) type {
            return extern struct {
                const Loc = ?*const anyopaque;

                context: P = null,
                callback: (?*const fn(ctx: P, err: [*:0]const u8, err_len: usize, source_location: Loc) callconv(.C) void) = null,
            };
        }
    };

    pub const in = struct {

        pub const Handle = extern struct {
            const Self = @This();
            const Opaque = c.libremidi_midi_in_handle;

            ptr: ?*const Opaque = null,


            extern fn libremidi_midi_in_new(conf: ?*const Config, api: ?*const Api.Config, out: *Self) c_int;
            pub fn init(conf: ?*const Config, api: ?*const Api.Config) !Self {

                var handle: Self = .{};
                switch (errnoFromInt(libremidi_midi_in_new(conf, api, &handle))) {
                    .SUCCESS => return handle,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_in_is_connected(self: Self) c_int;
            pub fn isConnected(self: Self) !bool {
                switch (libremidi_midi_in_is_connected(self)) {
                    0 => return false,
                    1 => return true,
                    -@intFromEnum(E.INVAL) => return error.InvalidArgument,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_in_absolute_timestamp(self: Self) Timestamp;
            pub fn getAbsoluteTimestamp(self: Self) !Timestamp {
                switch (libremidi_midi_in_absolute_timestamp(self)) {
                    -@intFromEnum(E.INVAL) => return error.InvalidArgument,
                    else => |ts| return ts,
                }
            }

            extern fn libremidi_midi_in_free(self: Self) c_int;
            pub fn free(self: Self) void {
                switch (libremidi_midi_in_free(self)) {
                    0 => return,
                    else => unreachable,
                }
            }
        };


        pub const port = struct {

            pub const Handle = extern struct {
                const Self = @This();
                const Opaque = c.libremidi_midi_in_port;

                ptr: ?*const Opaque = null,


                extern fn libremidi_midi_in_port_clone(self: Self, dest: *Self) c_int;
                pub fn clone(self: Self) !Self {

                    var handle: Self = .{};
                    switch (errnoFromInt(libremidi_midi_in_port_clone(self, &handle))) {
                        .SUCCESS => return handle,
                        .INVAL => return error.InvalidArgument,
                        else => unreachable,
                    }
                }

                extern fn libremidi_midi_in_port_free(self: Self) c_int;
                pub fn free(self: Self) void {
                    switch (libremidi_midi_in_port_free(self)) {
                        0 => return,
                        else => unreachable,
                    }
                }

                extern fn libremidi_midi_in_port_name(self: Self, name: *[*:0]const u8, len: *usize) c_int;
                pub fn getName(self: Self) ![:0]const u8 {

                    var name: [:0]const u8 = undefined;
                    switch (errnoFromInt(libremidi_midi_in_port_name(self, &name.ptr, &name.len))) {
                        .SUCCESS => return name,
                        .INVAL => return error.InvalidArgument,
                        else => unreachable,
                    }
                }
            };

            fn Callback(comptime P: type) type {
                return (?*const fn(ctx: P, port: port.Handle) callconv(.C) void);
            }
        };
    };

    pub const out = struct {

        pub const Handle = extern struct {
            const Self = @This();
            const Opaque = c.libremidi_midi_out_handle;

            ptr: ?*const Opaque = null,


            extern fn libremidi_midi_out_new(conf: ?*const Config, api: ?*const Api.Config, out: *Self) c_int;
            pub fn init(conf: ?*const Config, api: ?*const Api.Config) !Self {

                var handle: Self = .{};
                switch (errnoFromInt(libremidi_midi_out_new(conf, api, &handle))) {
                    .SUCCESS => return handle,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_is_connected(self: Self) c_int;
            pub fn isConnected(self: Self) !bool {
                switch (libremidi_midi_out_is_connected(self)) {
                    0 => return false,
                    1 => return true,
                    -@intFromEnum(E.INVAL) => return error.InvalidArgument,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_send_message(self: Self, msg: [*]const v1.Symbol, len: usize) c_int;
            pub fn sendMsg(self: Self, msg: []const v1.Symbol) !void {
                switch (errnoFromInt(libremidi_midi_out_send_message(self, msg.ptr, msg.len))) {
                    .SUCCESS => return,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_send_ump(self: Self, msg: [*]const v2.Symbol, len: usize) c_int;
            pub fn sendUmp(self: Self, msg: []const v2.Symbol) !void {
                switch (errnoFromInt(libremidi_midi_out_send_ump(self, msg.ptr, msg.len))) {
                    .SUCCESS => return,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_schedule_message(self: Self, ts: Timestamp, msg: [*]const v1.Symbol, len: usize) c_int;
            pub fn scheduleMsg(self: Self, ts: Timestamp, msg: []const v1.Symbol) !void {
                switch (errnoFromInt(libremidi_midi_out_schedule_message(self, ts, msg.ptr, msg.len))) {
                    .SUCCESS => return,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_schedule_ump(self: Self, ts: Timestamp, msg: [*]const v2.Symbol, len: usize) c_int;
            pub fn scheduleUmp(self: Self, ts: Timestamp, msg: []const v2.Symbol) !void {
                switch (errnoFromInt(libremidi_midi_out_schedule_ump(self, ts, msg.ptr, msg.len))) {
                    .SUCCESS => return,
                    .INVAL => return error.InvalidArgument,
                    .IO => return error.InputOutput,
                    else => unreachable,
                }
            }

            extern fn libremidi_midi_out_free(self: Self) c_int;
            pub fn free(self: Self) void {
                switch (libremidi_midi_out_free(self)) {
                    0 => return,
                    else => unreachable,
                }
            }
        };

        pub const port = struct {

            pub const Handle = extern struct {
                const Self = @This();
                const Opaque = c.libremidi_midi_out_port;

                ptr: ?*const Opaque = null,


                extern fn libremidi_midi_out_port_clone(self: Self, dest: *Self) c_int;
                pub fn clone(self: Self) !Self {

                    var handle: Self = .{};
                    switch (errnoFromInt(libremidi_midi_out_port_clone(self, &handle))) {
                        .SUCCESS => return handle,
                        .INVAL => return error.InvalidArgument,
                        else => unreachable,
                    }
                }

                extern fn libremidi_midi_out_port_free(self: Self) c_int;
                pub fn free(self: Self) void {
                    switch (libremidi_midi_out_port_free(self)) {
                        0 => return,
                        else => unreachable,
                    }
                }

                extern fn libremidi_midi_out_port_name(self: Self, name: *[*:0]const u8, len: *usize) c_int;
                pub fn getName(self: Self) ![:0]const u8 {

                    var name: [:0]const u8 = undefined;
                    switch (errnoFromInt(libremidi_midi_out_port_name(self, &name.ptr, &name.len))) {
                        .SUCCESS => return name,
                        .INVAL => return error.InvalidArgument,
                        else => unreachable,
                    }
                }
            };

            fn Callback(comptime P: type) type {
                return (?*const fn(ctx: P, port: port.Handle) callconv(.C) void);
            }
        };
    };

    pub const v1 = struct {
        const Ctx = ?*anyopaque;

        pub const Symbol = c.libremidi_midi1_symbol;
        pub const Message = [*]const Symbol;

        extern fn libremidi_midi1_available_apis(ctx: Ctx, cb: Api.Callback(Ctx)) void;
        pub const probeAvailableApis = libremidi_midi1_available_apis;

        fn Callback(comptime P: type) type {
            return extern struct {
                context: P = null,
                callback: (?*const fn(ctx: P, ts: Timestamp, msg: Message, len: usize) callconv(.C) void) = null,
            };
        }
    };

    pub const v2 = struct {
        const Ctx = ?*anyopaque;

        pub const Symbol = c.libremidi_midi2_symbol;
        pub const Message = [*]const Symbol;

        extern fn libremidi_midi2_available_apis(ctx: Ctx, cb: Api.Callback(Ctx)) void;
        pub const probeAvailableApis = libremidi_midi2_available_apis;

        fn Callback(comptime P: type) type {
            return extern struct {
                context: P,
                callback: (?*const fn(ctx: P, ts: Timestamp, msg: Message, len: usize) callconv(.C) void),
            };
        }
    };
};
