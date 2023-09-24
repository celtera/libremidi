# Feature matrix

This table shows which feature is supported by which backend so far, for advanced features.

It may be because the backend does not provide the ability at all, or because it has not been implemented yet.


|               | ALSA Raw | ALSA Seq | JACK | WinMM | UWP | CoreMIDI | Emscripten |
|---------------|----------|----------|------|-------|-----|----------|------------|
| Virtual ports | N/A      | Yes      | Yes  | N/A   | No  | Yes      | N/A        |
| Observer      | Yes      | Yes      | No   | Yes   | Yes | Yes      | Yes        |
| Chunking      | No       | No       | No   | No    | No  | No       | No         |