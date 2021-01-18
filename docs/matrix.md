# Feature matrix

This table shows which feature is supported by which backend so far, for advanced features.

It may be because the backend does not provide the ability at all (N/A), 
or because it has not been implemented yet (No).


|               | ALSA (Sequencer) | ALSA (Raw) | JACK | WinMM | UWP  | CoreAudio |
|---------------|------------------|------------|------|-------|------|-----------|
| Virtual ports | Yes              | No         | Yes  | N/A   | N/A  | Yes       |
| Observer      | WIP              | No         | No   | WIP   | Yes  | No        |
| Chunking      | N/A?             | No         | N/A? | N/A?  | N/A? | N/A?      |
| Poll          | N/A?             | Yes        | N/A  | N/A   | N/A? | N/A?      |
