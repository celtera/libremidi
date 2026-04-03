#!/usr/bin/env python
"""
Tests for the raw I/O backend and all send_message / send_ump overloads.
Run with: python test_rawio.py
"""
import pylibremidi as lm
import sys

failures = []

def check(name, condition):
    if not condition:
        failures.append(name)
        print(f"  FAIL: {name}")
    else:
        print(f"  ok: {name}")

# ============================================================
# Helper: create a MIDI 1 rawio loopback pair
# ============================================================
def make_midi1_loopback():
    """Returns (midi_in, midi_out, received_messages) with direct loopback."""
    stored_cb = [None]
    received = []

    in_config = lm.InputConfiguration()
    in_config.on_message = lambda msg: received.append(msg)
    in_config.direct = True

    rawio_in = lm.RawioInputConfiguration()
    rawio_in.set_receive_callback = lambda cb: stored_cb.__setitem__(0, cb)
    rawio_in.stop_receive = lambda: stored_cb.__setitem__(0, None)

    midi_in = lm.MidiIn(in_config, rawio_in)
    midi_in.open_virtual_port("test_in")

    out_config = lm.OutputConfiguration()
    rawio_out = lm.RawioOutputConfiguration()
    def write(data):
        if stored_cb[0]:
            stored_cb[0](data, 0)
        return lm.Error()
    rawio_out.write_bytes = write

    midi_out = lm.MidiOut(out_config, rawio_out)
    midi_out.open_virtual_port("test_out")

    return midi_in, midi_out, received

# ============================================================
# Helper: create a MIDI 2 (UMP) rawio loopback pair
# ============================================================
def make_ump_loopback():
    """Returns (midi_in, midi_out, received_umps) with direct loopback."""
    stored_cb = [None]
    received = []

    in_config = lm.UmpInputConfiguration()
    in_config.on_message = lambda msg: received.append(msg)
    in_config.direct = True

    rawio_in = lm.RawioUmpInputConfiguration()
    rawio_in.set_receive_callback = lambda cb: stored_cb.__setitem__(0, cb)
    rawio_in.stop_receive = lambda: stored_cb.__setitem__(0, None)

    midi_in = lm.MidiIn(in_config, rawio_in)
    midi_in.open_virtual_port("test_ump_in")

    out_config = lm.OutputConfiguration()
    rawio_out = lm.RawioUmpOutputConfiguration()
    def write(data):
        if stored_cb[0]:
            stored_cb[0](data, 0)
        return lm.Error()
    rawio_out.write_ump = write

    midi_out = lm.MidiOut(out_config, rawio_out)
    midi_out.open_virtual_port("test_ump_out")

    return midi_in, midi_out, received


# ============================================================
# Test: API detection
# ============================================================
print("--- API detection ---")
midi_in, midi_out, _ = make_midi1_loopback()
check("midi1 input api is RAW_IO", midi_in.get_current_api() == lm.API.RAW_IO)
check("midi1 output api is RAW_IO", midi_out.get_current_api() == lm.API.RAW_IO)

midi_in, midi_out, _ = make_ump_loopback()
check("ump input api is RAW_IO_UMP", midi_in.get_current_api() == lm.API.RAW_IO_UMP)
check("ump output api is RAW_IO_UMP", midi_out.get_current_api() == lm.API.RAW_IO_UMP)


# ============================================================
# Test: send_message overloads (MIDI 1)
# ============================================================
print("\n--- send_message overloads ---")

# send_message(b0) - 1 byte
midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message(0xFA)  # Start (not filtered by default, unlike Active Sensing)
check("send_message(b0): 1 message received", len(received) == 1)
check("send_message(b0): correct byte", received[0].bytes[0] == 0xFA)

# send_message(b0, b1) - 2 bytes
midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message(0xC0, 42)  # Program Change
check("send_message(b0,b1): 1 message received", len(received) == 1)
check("send_message(b0,b1): correct status", received[0].bytes[0] == 0xC0)
check("send_message(b0,b1): correct data", received[0].bytes[1] == 42)

# send_message(b0, b1, b2) - 3 bytes
midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message(0x90, 60, 100)  # Note On
check("send_message(b0,b1,b2): 1 message received", len(received) == 1)
check("send_message(b0,b1,b2): correct status", received[0].bytes[0] == 0x90)
check("send_message(b0,b1,b2): correct note", received[0].bytes[1] == 60)
check("send_message(b0,b1,b2): correct velocity", received[0].bytes[2] == 100)

# send_message(list) - vector overload
midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message([0xB0, 7, 80])  # CC
check("send_message(list): 1 message received", len(received) == 1)
check("send_message(list): correct status", received[0].bytes[0] == 0xB0)
check("send_message(list): correct cc", received[0].bytes[1] == 7)
check("send_message(list): correct value", received[0].bytes[2] == 80)

# send_message(Message) - message object overload
midi_in, midi_out, received = make_midi1_loopback()
msg = lm.Message()
msg.bytes = [0xE0, 0x00, 0x40]  # Pitch Bend center
midi_out.send_message(msg)
check("send_message(Message): 1 message received", len(received) == 1)
check("send_message(Message): correct status", received[0].bytes[0] == 0xE0)
check("send_message(Message): correct lsb", received[0].bytes[1] == 0x00)
check("send_message(Message): correct msb", received[0].bytes[2] == 0x40)

# schedule_message(timestamp, list)
midi_in, midi_out, received = make_midi1_loopback()
midi_out.schedule_message(12345, [0x90, 48, 127])
check("schedule_message(ts,list): 1 message received", len(received) == 1)
check("schedule_message(ts,list): correct note", received[0].bytes[1] == 48)


# ============================================================
# Test: send_ump overloads (MIDI 2 / UMP)
# ============================================================
print("\n--- send_ump overloads ---")

# send_ump(u0) - 1 word (System Real-Time Start, type 1 - not subject to MIDI1->2 upconversion)
midi_in, midi_out, received = make_ump_loopback()
midi_out.send_ump(0x10FA0000)  # System RT Start, group 0
check("send_ump(u0): 1 message received", len(received) == 1)
check("send_ump(u0): correct word0", received[0].data[0] == 0x10FA0000)

# send_ump(u0, u1) - 2 words (MIDI 2 channel voice)
midi_in, midi_out, received = make_ump_loopback()
midi_out.send_ump(0x4090003C, 0xC0000000)  # Note On
check("send_ump(u0,u1): 1 message received", len(received) == 1)
check("send_ump(u0,u1): correct word0", received[0].data[0] == 0x4090003C)
check("send_ump(u0,u1): correct word1", received[0].data[1] == 0xC0000000)

# send_ump(u0, u1, u2, u3) - 4 words (SysEx8, type 5 - need ignore_sysex=False)
stored_cb = [None]
received_4w = []

in_config = lm.UmpInputConfiguration()
in_config.on_message = lambda msg: received_4w.append(msg)
in_config.ignore_sysex = False
in_config.direct = True

rawio_in = lm.RawioUmpInputConfiguration()
rawio_in.set_receive_callback = lambda cb: stored_cb.__setitem__(0, cb)
rawio_in.stop_receive = lambda: stored_cb.__setitem__(0, None)

midi_in_4w = lm.MidiIn(in_config, rawio_in)
midi_in_4w.open_virtual_port("test_ump_4w")

out_config = lm.OutputConfiguration()
rawio_out = lm.RawioUmpOutputConfiguration()
def write_4w(data):
    if stored_cb[0]:
        stored_cb[0](data, 0)
    return lm.Error()
rawio_out.write_ump = write_4w

midi_out_4w = lm.MidiOut(out_config, rawio_out)
midi_out_4w.open_virtual_port("test_ump_4w")

midi_out_4w.send_ump(0x50010000, 0x01020304, 0x05060708, 0x090A0B0C)
check("send_ump(u0,u1,u2,u3): 1 message received", len(received_4w) == 1)
if received_4w:
    check("send_ump(u0,u1,u2,u3): correct word0", received_4w[0].data[0] == 0x50010000)
    check("send_ump(u0,u1,u2,u3): correct word1", received_4w[0].data[1] == 0x01020304)
    check("send_ump(u0,u1,u2,u3): correct word2", received_4w[0].data[2] == 0x05060708)
    check("send_ump(u0,u1,u2,u3): correct word3", received_4w[0].data[3] == 0x090A0B0C)

# send_ump(list) - vector overload
midi_in, midi_out, received = make_ump_loopback()
midi_out.send_ump([0x4090003C, 0xC0000000])
check("send_ump(list): 1 message received", len(received) == 1)
check("send_ump(list): correct word0", received[0].data[0] == 0x4090003C)
check("send_ump(list): correct word1", received[0].data[1] == 0xC0000000)

# send_ump(Ump) - ump object overload
midi_in, midi_out, received = make_ump_loopback()
ump = lm.Ump()
ump.data = (0x4090003C, 0xC0000000, 0, 0)
midi_out.send_ump(ump)
check("send_ump(Ump): 1 message received", len(received) == 1)
check("send_ump(Ump): correct word0", received[0].data[0] == 0x4090003C)
check("send_ump(Ump): correct word1", received[0].data[1] == 0xC0000000)

# schedule_ump(timestamp, list)
midi_in, midi_out, received = make_ump_loopback()
midi_out.schedule_ump(99999, [0x4090003C, 0xC0000000])
check("schedule_ump(ts,list): 1 message received", len(received) == 1)
check("schedule_ump(ts,list): correct word0", received[0].data[0] == 0x4090003C)


# ============================================================
# Test: multiple messages in sequence
# ============================================================
print("\n--- multiple messages ---")

midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message(0x90, 60, 100)
midi_out.send_message(0x80, 60, 0)
midi_out.send_message(0xB0, 7, 80)
midi_out.send_message([0xB0, 10, 64])
check("multiple midi1: 4 messages", len(received) == 4)
check("multiple midi1: msg0 note on", received[0].bytes[0] == 0x90)
check("multiple midi1: msg1 note off", received[1].bytes[0] == 0x80)
check("multiple midi1: msg2 cc7", received[2].bytes[1] == 7)
check("multiple midi1: msg3 cc10", received[3].bytes[1] == 10)

midi_in, midi_out, received = make_ump_loopback()
midi_out.send_ump(0x4090003C, 0xC0000000)
midi_out.send_ump(0x4080003C, 0x00000000)
midi_out.send_ump([0x40B00007, 0x80000000])
check("multiple ump: 3 messages", len(received) == 3)


# ============================================================
# Test: close_port calls stop_receive
# ============================================================
print("\n--- close_port ---")

stopped = [False]
in_config = lm.InputConfiguration()
in_config.on_message = lambda msg: None
in_config.direct = True

rawio_in = lm.RawioInputConfiguration()
rawio_in.set_receive_callback = lambda cb: None
rawio_in.stop_receive = lambda: stopped.__setitem__(0, True)

midi_in = lm.MidiIn(in_config, rawio_in)
midi_in.open_virtual_port("test")
check("stop_receive not called yet", not stopped[0])
midi_in.close_port()
check("stop_receive called on close", stopped[0])


# ============================================================
# Test: Error default constructor
# ============================================================
print("\n--- Error ---")
err = lm.Error()
check("default Error is falsy (no error)", not err)


# ============================================================
# Test: edge cases - large values, boundary values
# ============================================================
print("\n--- edge cases ---")

# Maximum MIDI 1 byte values
midi_in, midi_out, received = make_midi1_loopback()
midi_out.send_message(0xFF)  # System Reset
check("send_message(0xFF): received", len(received) == 1)
check("send_message(0xFF): correct", received[0].bytes[0] == 0xFF)

# UMP with large uint32 values (ensure no sign/overflow issues)
# UMP with max values in data portion (type 4, 2 words)
midi_in, midi_out, received = make_ump_loopback()
midi_out.send_ump(0x409F7F7F, 0xFFFFFFFF)
check("send_ump(large values): received", len(received) == 1)
check("send_ump(large values): word0", received[0].data[0] == 0x409F7F7F)
check("send_ump(large values): word1", received[0].data[1] == 0xFFFFFFFF)


# ============================================================
# Summary
# ============================================================
print(f"\n{'='*40}")
if failures:
    print(f"FAILED: {len(failures)} test(s)")
    for f in failures:
        print(f"  - {f}")
    sys.exit(1)
else:
    print("All tests passed.")
    sys.exit(0)
