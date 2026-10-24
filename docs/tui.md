# Terminal workroom

Run `just tui` (or `xmake run hrm-tui`). The terminal must be at least 112 columns
by 32 rows; 140 by 48 or larger gives the command stack more room. Mouse input
is supported alongside keyboard navigation.
The shared procedural 8-bit soundtrack starts automatically. **M** toggles
music and **N** toggles sound effects; `--no-audio` disables audio initialization.
See [audio.md](audio.md) for synthesis, timing, and audition controls.

Text, borders, and card backgrounds inherit the terminal's default foreground
and background. Highlights use its ANSI cyan, yellow, blue, and red palette
entries, so the interface follows the terminal's configured theme without
forcing a dark background. Clawd's introductory rainbow uses RGB colors.

The left workroom displays the remaining input belt, the robot's current hand,
numbered floor slots, and the output belt. The right panel shows the level task,
target output, available instruction palette, and scrollable command cards.
Playback controls sit below the left workroom; the command panel uses the full
remaining terminal height, with editing controls and hints at its own bottom.
The gold card with a triangle is the current instruction. The blue card is the
editor selection; selection and execution position are independent.

## Choosing levels and saving solutions

The game opens with a keyboard-driven level picker. Use Up/Down and Enter to
open a level or resume its draft. Press `/` in the workroom to reopen the picker;
playback pauses while choosing. Esc returns to the workroom. The old title bar
and level dropdown are removed, leaving the workroom the full terminal height.
`--level ID` chooses the initially highlighted entry. `--preview` still renders
the workroom directly for layout inspection.

Completed levels show a badge. Press **L** or choose **Load solution** to replace
that level's in-memory draft with its last successful program, reset to step zero.
Enter preserves the draft instead. Winning programs are saved automatically as
`~/.config/hrm/level-ID.yaml`, or `$XDG_CONFIG_HOME/hrm/level-ID.yaml` when an
absolute XDG configuration directory is set. Unsolved drafts remain in memory.
The saved YAML contains instruction text, resolved numeric jump destinations,
execution-relevant level data, and the successful attempt count. Writes replace
files atomically. No terminal layout or animation state is stored.

On startup, saves are checked against the current level data and replayed before
being marked completed. Changed levels invalidate old completion; corrupt files
are reported in the picker and do not prevent playing. Verification is bounded
to one million instruction attempts, which is also the save limit. Saving errors
are shown in the workroom without discarding the successful run.

## Building a program

Drag an instruction from the palette into the program stack. A floating rounded
card keeps the original card's size and contents, with the exact grabbed point
anchored to the pointer. It clamps at terminal edges and disappears on drop or
cancellation. The insertion line
shows where it will be placed; the program changes only on release. Drop outside
the stack or press Esc to cancel. Drag existing command cards to reorder them.
The stack scrolls when a dragged card is held near its top or bottom edge.

Click a command card, then click an instruction in the palette to insert after
it, or use Tab and Enter on palette buttons. The palette contains only
instructions allowed in the selected level.
For floor instructions, select the card, edit the boxed floor index, then press
Enter or click **Set floor**. Delete removes the selected card. A plain drop
prompt marks the append position; ordinary insertions do not leave empty cards.

Select a jump card and press **T** or **Link [T]**. Select its destination with
Up/Down then Enter, or click the destination. Linking to an ordinary command
creates an empty landing card immediately before it. Pixel-block arrows point
from jump cards to these empty landing cards. Multiple jumps can share a
landing card.

Landing cards are editor objects rather than executable instructions. A jump
resolves to the first command below its landing card. Inserting or deleting
commands rebases the compiled target automatically. Inserting directly below a
landing card makes the new command its destination. An unlinked jump, or a
landing card with no command below it, becomes a jump to target 0, so
the existing kernel reports an error only if execution reaches that command.

Deleting a landing card disconnects its incoming jumps. Landing cards with no
incoming jumps are removed automatically. Reordering keeps row IDs and jump
links intact; a landing still resolves to the command immediately below its
position. Editing commands or links resets machine state. Programs and playback positions are kept in memory
per level while the TUI is open.

## Playback

The robot walks between the input belt, output belt, and addressed floor cells.
Each instruction uses a walk, interaction, and settle sequence. Legs alternate
while walking; the robot reaches during interaction and its held block follows
the pickup, delivery, or copying path.

Clawd keeps its compact three-line silhouette. On entry, a rainbow gradient
flows across its pixels until the first keypress, mouse click, or scroll.
Pointer movement alone leaves the rainbow running. After interaction it uses
the terminal's yellow accent; reset and level changes do not restart the rainbow.
An unexecuted empty program does not trigger failure red.
While idle, it blinks briefly
every four seconds and stretches its feet between blinks. Walking alternates
wide and narrow foot placements with a small bob. Block interactions dip the
body and tuck the feet; arithmetic taps alternate feet. Taken jumps hop with
tucked feet, while an untaken conditional only taps a foot. An empty inbox
shows a question mark. Errors and incorrect output turn Clawd red while keeping
its normal silhouette. The red color persists until rewind, reset, or a program
edit clears the failure. Idle gestures use their own slow clock; instruction
gestures follow playback speed.

| Instruction | Motion |
| --- | --- |
| `inbox` | Walk left, pick up the next block, and lift it into the hand. An empty belt ends with an empty-inbox signal. |
| `outbox` | Walk right, lower the held block onto the output belt, and release it. |
| `copyto X` | Approach floor X and place a moving copy, retaining the held value. |
| `copyfrom X` | Approach floor X and lift a copy; the floor block remains. |
| `add X` / `sub X` | Approach floor X, lean/reach, and show the operand and resulting hand value. |
| `jump` / `jumpifzero` / `jumpifnegative` | Hop in place when taken; an untaken conditional taps a foot. Control-flow changes do not teleport the robot. |
| Runtime error | Stay in place and turn red; no pickup or placement occurs. |

Motion uses the existing speed slider. Pause freezes the pose and resumes from
the same phase. Manual Next animates one attempt; pressing Next again settles
the current attempt and starts the next one. Previous reconstructs both kernel
state and the prior physical location; Reset returns the robot home. Editing a
program also resets its pose. Large floors page to the addressed cell.

The kernel still commits one deterministic step. The C++20 presentation model
captures its before/after states and reveals the new hand, floor, and conveyors
at the interaction's transfer point (70% of the animation). The current command
stays highlighted until settling. Robot interpolation and rendering do not enter
the C++11 kernel or OJ submission. A full-value hand readout remains visible
even when a block card abbreviates a long number.

| Control | Keyboard | Behavior |
| --- | --- | --- |
| Reset | R | Restore input, clear output, hand, floor and execution counters; keep the edited program. |
| Previous | Left | Restore the state before the last attempted instruction, including after success or a fault. |
| Play / Pause | Space | Run continuously or pause. Play after termination starts again from the beginning. |
| Next | Right | Pause and attempt one instruction. |
| Speed | Click/drag the slider, or focus it and use Left/Right | Set 1-12 instructions per second. |
| Select card | Up/Down, mouse wheel, or click | Move the editor selection; the stack scrolls to it. |
| Focus | Tab / Shift-Tab | Move between interactive components. |
| Quit / cancel | Q / Esc | Exit; Esc cancels an active drag or pending link first. |

While the floor index field is focused, normal text editing takes precedence
over playback shortcuts. Continuous playback follows the execution cursor in
the command stack. Previous reconstructs an earlier state by deterministic
replay, so its work grows with the number of prior execution steps. Playback
timing runs on the UI thread with no background mutations of the kernel.

## YAML configuration

`assets/game.yaml` contains the three assignment levels, **Triple it** (level 4),
and **Same sign** (level 5).
Every level opens with an empty command editor. The first three levels are
tested against the OJ built-in data; additional game levels live in YAML.
The build copies the configuration beside the executable under `assets/`, so
the TUI can also start outside the repository. A working-directory
`assets/game.yaml` takes precedence; `--config PATH` selects a file explicitly.

```yaml
version: 1
title: Human Resource Machine
levels:
  - id: 1
    title: First day
    description: Send all input blocks to the output in order.
    input: [1, 2]
    target: [1, 2]
    floor_size: 0
    instructions: [inbox, outbox]
```

IDs must be positive and unique. Numeric values must be signed 64-bit integers;
floor size must be between 0 and 1024. Available instruction names are validated
on load. Players build their programs in the command editor and create landing
cards by linking jumps.
An optional `initial_floor` map preloads occupied cells, including zero:

```yaml
floor_size: 6
initial_floor: {4: 0, 5: 1}
```

Unspecified slots start empty. Slots remain writable, and Reset and rewind
restore the configured initial values. Presets are part of saved-solution
verification, so changing a constant invalidates older completion data.
**Triple it** multiplies each input by 3 using one working slot, covering positive,
negative, and zero inputs. It uses the existing course instruction set.
**Same sign** consumes pairs of nonzero inputs and outputs 0 for two positive
or two negative values, and 1 for opposite signs. Its input covers all four
sign combinations; slots 4 and 5 initially contain 0 and 1.
Level 5 introduces `jumpifnegative N`: jump to instruction N when the hand holds
a negative value, otherwise continue. It retains the held value, requires a
nonempty hand, and validates the destination even when the jump is not taken.
The new instruction supports the same landing cards, drag operations, target
rebasing, and playback animation as other jumps. Future YAML levels can opt in
by listing `jumpifnegative` under `instructions`. OJ levels 1–3 keep their original
permissions and reject this extension. Existing saves for unchanged levels keep
their previous instruction-permission identity.
YAML loading lives in the C++20 application library; the C++11 kernel and
single-file OJ submission do not depend on YAML or FTXUI.

```bash
xmake run hrm-tui --config assets/game.yaml --level 2
xmake run hrm-tui --preview --level 2 --width 140 --height 52
just test-tui
just test-tui-drag
just test
```

`--preview` renders one terminal frame without starting an interactive session.
`just test-tui` checks YAML validation, landing-card compilation, jump rebasing,
and forward/backward/reset playback, including deterministic robot animation
phases, destinations, and data-transfer timing. `just test-tui-drag` uses an isolated tmux
terminal to check real mouse press/move/release events, insertion markers,
start/middle/end drops, cancellation, reordering, jump links, scrolling, walking,
pause/resume, rewind/reset, and error animations. The
Nix shell includes tmux for this check. The terminal tests also cover startup
selection, slash navigation, saved completion, and solution loading after restart;
their configuration directories are isolated from the player's real progress.
