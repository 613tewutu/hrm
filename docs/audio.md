# Procedural chiptune audio

TUI and GUI share `hrm-audio`, a C++20 audio module backed by raylib. The C++11
kernel and packed OJ submission have no audio dependency. Both frontends require
raylib, including TUI-only builds. No sound files or additional audio libraries
are required at runtime. `just bundle-tui` includes the audio code and score in
the same executable as the embedded levels; audition WAV exports are optional.

The original eight-bar background loop runs at 112 BPM for about 17 seconds.
It combines a 25% pulse melody, pulse arpeggios, triangle bass, and noise
percussion over C / Am / F / G. Samples have an 8-bit-style amplitude grid,
stored as 48 kHz 16-bit mono PCM for playback. Short attack/release envelopes
avoid abrupt waveform cuts. The loop is composed once and streamed in fixed
buffers; changing music playback does not change simulation speed.

Fourteen cached effects cover selection, insertion, removal, pickup, delivery,
floor storage/copying, addition/subtraction, jumps, an empty inbox, errors,
success, and reset. Three playback aliases per effect allow bounded overlap.
Frequent ordinary cues have a 55 ms per-effect cooldown. Success and error
briefly lower the music volume. Input/output interactions are panned slightly
left/right. Clawd's idle gestures remain silent.

TUI block interaction and jump cues fire once when animation crosses the 70%
transfer phase, including frames that skip across that phase. Errors sound when
the instruction faults; terminal success/failure cues follow visual settling.
Pause stops effects but leaves background music running. Rewind is silent;
reset, editing, and level changes cancel active effects. Menu music continues
while choosing levels.

Press **M** to toggle music and **N** to toggle effects. The TUI footer and level
picker show their status. In the GUI sound room, use Left/Right to select an
effect and Space to play it. The GUI currently provides this audio preview,
rather than the full graphical workroom.

`--no-audio` skips device initialization in either frontend. TUI `--preview`
also always disables audio. An unavailable audio device leaves the frontend
usable in silence. Toggles are per process and are not saved as level progress.

Run `just test-audio` to check deterministic synthesis, signal energy, DC
offset, headroom, silent endpoints, distinct cues, and disabled-device controls.
It also writes an audition bank to `build/audio-preview/`, including `music.wav`
and numbered effect WAVs in `Cue` enum order. This export is for listening and
debugging; the game generates the same sounds directly in memory.

Run `just tui` for the terminal game or `just gui` for the graphical sound room.
Frontends own exactly one `hrm_audio::Audio`; call `update()` each frame and
`play(Cue, pan)` for semantic actions. The module releases audio resources when
the owning frontend exits.
