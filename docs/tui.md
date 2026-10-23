# Terminal workroom

Run `just tui` (or `xmake run hrm-tui`). The terminal must be at least 112 columns
by 32 rows; 140 by 48 or larger gives the command stack more room. Mouse input
is supported alongside keyboard navigation.

Text, borders, and card backgrounds inherit the terminal's default foreground
and background. Highlights use its ANSI cyan, yellow, blue, and red palette
entries, so the interface follows the terminal's configured theme without
forcing a dark background or fixed RGB colors.

The left workroom displays the remaining input belt, the robot's current hand,
numbered floor slots, and the output belt. The right panel shows the level task,
target output, available instruction palette, and scrollable command cards.
Playback controls sit below the left workroom; the command panel uses the full
remaining terminal height, with editing controls and hints at its own bottom.
The gold card with a triangle is the current instruction. The blue card is the
editor selection; selection and execution position are independent.

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
landing card with no command below it, becomes `jump 0` (or `jumpifzero 0`), so
the existing kernel reports an error only if execution reaches that command.

Deleting a landing card disconnects its incoming jumps. Landing cards with no
incoming jumps are removed automatically. Reordering keeps row IDs and jump
links intact; a landing still resolves to the command immediately below its
position. Editing commands or links resets machine state. Programs and playback positions are kept in memory
per level while the TUI is open.

## Playback

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

`assets/game.yaml` contains the three assignment levels. Every level opens with
an empty command editor. The supplied YAML level data is tested against the OJ built-in data.
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
and forward/backward/reset playback. `just test-tui-drag` uses an isolated tmux
terminal to check real mouse press/move/release events, insertion markers,
start/middle/end drops, cancellation, reordering, jump links and scrolling. The
Nix shell includes tmux for this check. Player progress and program persistence
are future additions.
