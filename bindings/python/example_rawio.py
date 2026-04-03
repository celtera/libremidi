#!/usr/bin/env python
"""
Raw I/O backend example for pylibremidi.

Demonstrates a loopback: bytes written by MidiOut are fed directly
back into MidiIn, simulating a serial wire connection.
This is the pattern you'd use for custom transports (serial, SPI, USB HID, etc.)
"""
import pylibremidi as lm


def midi1_example():
    print("=== MIDI 1 Raw I/O ===")

    # The library will give us a callback to call when bytes arrive
    stored_cb = [None]

    # Input: receive and print parsed MIDI messages
    in_config = lm.InputConfiguration()
    in_config.on_message = lambda msg: print(f"  Received: {msg}")
    in_config.direct = True  # Call Python callbacks directly (no poll needed)

    rawio_in = lm.RawioInputConfiguration()
    rawio_in.set_receive_callback = lambda cb: stored_cb.__setitem__(0, cb)
    rawio_in.stop_receive = lambda: stored_cb.__setitem__(0, None)

    midi_in = lm.MidiIn(in_config, rawio_in)
    midi_in.open_virtual_port("rawio_in")

    # Output: loopback written bytes into the input
    out_config = lm.OutputConfiguration()

    def loopback_write(data):
        if stored_cb[0]:
            stored_cb[0](data, 0)
        return lm.Error()

    rawio_out = lm.RawioOutputConfiguration()
    rawio_out.write_bytes = loopback_write

    midi_out = lm.MidiOut(out_config, rawio_out)
    midi_out.open_virtual_port("rawio_out")

    # Send some MIDI messages - they loop back through the rawio transport
    print("Sending Note On C4...")
    midi_out.send_message(0x90, 60, 100)

    print("Sending Note Off C4...")
    midi_out.send_message(0x80, 60, 0)

    print("Sending CC Volume=80...")
    midi_out.send_message(0xB0, 7, 80)

    # close_port breaks the reference cycle: midi_out -> write -> stored_cb -> C++ callback -> midi_in
    midi_in.close_port()
    midi_out.close_port()


def midi2_example():
    print("\n=== MIDI 2 Raw I/O (UMP) ===")

    stored_cb = [None]

    ump_in_config = lm.UmpInputConfiguration()
    ump_in_config.on_message = lambda msg: print(f"  Received UMP: {msg}")
    ump_in_config.direct = True

    rawio_ump_in = lm.RawioUmpInputConfiguration()
    rawio_ump_in.set_receive_callback = lambda cb: stored_cb.__setitem__(0, cb)
    rawio_ump_in.stop_receive = lambda: stored_cb.__setitem__(0, None)

    midi_in = lm.MidiIn(ump_in_config, rawio_ump_in)
    midi_in.open_virtual_port("rawio_ump_in")

    ump_out_config = lm.OutputConfiguration()

    def loopback_write_ump(data):
        if stored_cb[0]:
            stored_cb[0](data, 0)
        return lm.Error()

    rawio_ump_out = lm.RawioUmpOutputConfiguration()
    rawio_ump_out.write_ump = loopback_write_ump

    midi_out = lm.MidiOut(ump_out_config, rawio_ump_out)
    midi_out.open_virtual_port("rawio_ump_out")

    # Send a MIDI 2.0 Note On UMP (group 0, channel 0, note 60)
    print("Sending UMP Note On...")
    midi_out.send_ump(0x4090003C, 0xC0000000)

    midi_in.close_port()
    midi_out.close_port()


midi1_example()
midi2_example()
print("\nDone.")
