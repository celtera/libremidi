# MIDI file I/O usage

## Reading a .mid file

See `midifile_dump.cpp` for a more complete example.

```
// Read raw from a MIDI file
std::ifstream file{"path/to/a.mid, std::ios::binary};

std::vector<uint8_t> bytes;
bytes.assign(std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>());

// Initialize our reader object
libremidi::reader r;

// Parse
libremidi::reader::parse_result result = r.parse(bytes);

// If parsing succeeded, use the parsed data
if(result != libremidi::reader::invalid) {
  for(auto& track : r.tracks) {
    for(auto& event : t.events) {
      std::cout << (int) event.m.bytes[0] << '\n';
    }
  }
}
```

## Writing a .mid file