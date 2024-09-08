#!/usr/bin/env python3.13
import pylibremidi as lm
observer_config = lm.ObserverConfiguration()
observer = lm.Observer(observer_config, lm.API.ALSA_SEQ)

pi = observer.get_input_ports()
if len(pi) == 0:
    print("No input available")
    exit(1)

po = observer.get_output_ports()
if len(po) == 0:
    print("No input available")
    exit(1)

midi_out = lm.MidiOut()
midi_out.open_port(po[0])

in_config = lm.UmpInputConfiguration()
in_config.on_message = lambda msg: print(msg.data[0])

midi_in = lm.MidiIn(in_config)
midi_in.open_port(pi[0])

while True:
    midi_in.poll()
