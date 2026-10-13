# IBM 650 Emulator

## Overview

This document covers the emulator's architecture: the machine as modelled,
the timing model, the instruction set with manual citations, and the module
layout. See [README.md](README.md) for build instructions.

Manual references: **GI** = *General Information* (24-5000-0), **BOC** =
*Basic Operation Codes* (24-5002-0), **OM** = *650 Manual of Operation*
(22-6060-2), **RP** = *Card Reader and Punch* (24-5001-0). Test names in `tests/` cite the BOC
figure each golden vector comes from.

## Architecture

**Step-driven, not loop-driven.** `i650_step(machine)` reads, interprets and
executes one instruction and returns a status. The host drives the loop at
its own pace; the core never blocks.

**No global state.** Everything lives in a heap-allocated `struct I650`;
tests create as many as they need.

**Pure logic in core.** No `<stdio.h>` in `core/`. Accumulator arithmetic
(`accum.c`) knows nothing about the drum or time; the drum (`drum.c`) knows
nothing about instructions.

## Module Layout

```
core/
  types.h   — word type, digit-position helpers, instruction fields,
              address validity, accumulator and machine structs
  accum.c/h — 20-digit sign-magnitude adder, multiply/divide loops, shifts
  drum.c/h  — drum_wait_for() (the one timing primitive), storage access
  cpu.c/h   — fetch/decode/execute, per-op timing, console reset/start,
              inspection API, step record, counters, access marks
  cards.c/h — cards and deck files, the fixed-board card layout,
              the 533's hopper, feed and stackers (RD/WR are in cpu.c)
  opcodes.c/h — SOAP mnemonic table
  program.c/h — .650 program file parser and loader
  selfload.c/h — a .650 program as a self-loading deck (Andree's
              one-instruction-per-card load routine)
host/cli/
  main.c    — arguments, batch mode (--run)
  repl.c    — the console: commands, run loop and pacing, walk mode,
              editor hook
  display.c — trace lines and timing bar, panel, memory lines, drum map
  program_loader.c — read/parse/load .650 files, read and write deck
              files (shared by main and repl)
tests/
  test_types.c, test_accum.c, test_cpu.c — BOC register-diagram vectors
  test_timing.c — Optimum Program Chart and the BOC pp.24-29 examples,
                  step record and idle counters
  test_program.c — mnemonics, .650 parser and loader
  test_cards.c — card layout, deck files, the read feed, RD/WR,
                 load cards and the console bootstrap
  test_selfload.c — deck layout, its limits, booting a generated deck
  examples.sh — the example programs end to end
```

`make test` also runs `tests/examples.sh`: every program in `examples/`
in batch mode (with its `.cards` data deck when there is one), checking
how it stops and what it punches; then each one again, booted from the
console off its own generated deck, which must stop the same way and
punch the same cards.

## The Machine

**Word**: 10 decimal digits plus sign, sign-magnitude; minus zero exists.
Digit positions are numbered the IBM way: 1 = units … 10 = high-order.
Internally a word is a binary magnitude below 10^10 plus a sign flag — the
machine's bi-quinary code is not modelled in storage (it is planned as an
optional display in the console).

**Instruction**: `OP(2) D(4) I(4)` — operation code, data address,
instruction address (address of the next instruction).

**Addresses**: 0000-1999 drum; 8000 storage-entry console switches; 8001
distributor; 8002 lower accumulator; 8003 upper accumulator. Anything else
is a storage-selection stop.

**Accumulator**: 20 digits with one sign, addressable as upper (8003) and
lower (8002) halves. After a divide that keeps its remainder, the remainder
(lower) carries its own sign (the dividend's) while the quotient (upper)
carries the quotient's.

**Distributor**: the one-word buffer every drum word passes through; LDD
and the store instructions use it directly.

## Timing Model

Time is counted in word times of 0.096 ms; 50 word times make one drum
revolution (4.8 ms). At word time *t* the word at drum position *t* mod 50
— in every band of 50 — is under the heads.

Each instruction is read at the first moment its address comes round
(`drum_wait_for`). From the read time *n*, the Optimum Program Chart (BOC
p.23) gives two offsets: the earliest time the data can be read, and the
earliest time the next instruction can be read after that. The emulator
applies them literally, so an instruction placed one word too early costs a
full revolution, and the manual's optimized examples run with zero wait.

- 8000/8001 are available immediately; 8002 is read on even word times and
  8003 on odd ones.
- Add/subtract charge 2 extra word times when a non-reset operation
  changes the accumulator's sign (the complement cycle).
- Multiply time comes from the additions the loop actually performed;
  divide time from the sum of the quotient digits.
- Shifts set an interlock: the next instruction may be read from the
  chart's low limit but doesn't start until the high limit.

## The Console

A plain line-oriented REPL with no full-screen display — what makes it feel
like the machine is what it shows, not how it looks.

**Each step is recorded.** `i650_step` fills `last_step`: when the
machine became ready for the instruction, when it was read, when its data
was reached and could first have been reached, and when it finished. The
trace bar is drawn from that record, and the idle counters are the sums of
its waiting periods (instruction waits and data waits kept separately).

**Runs are paced on the host clock.** Native speed compares emulated time
(word times × 0.096 ms) with elapsed real time and sleeps while ahead;
the sleep is a `poll()` on stdin, so pressing Enter stops the run without
putting the terminal into raw mode. Walk mode does the same with the drum
slowed to one revolution per 2 seconds, printing the bar one word time at
a time.

**Starting a program.** Computer Reset sets the address register to 8000,
so Program Start executes the instruction in the storage-entry switches
(GI p.14) — on the real machine typically a read-card or branch
instruction. A `.650` file's `start` line is a shortcut that sets the
address register directly, like the console's transfer key.

## Instruction Set

| Code | Mnemonic | Operation | BOC figures |
|------|----------|-----------|-----|
| 00 | NOP | No operation | — |
| 01 | HLT | Stop (if the Programmed switch is on STOP) | — |
| 10 / 11 | AUP / SUP | Add / subtract to upper | Figs 5, 9 |
| 15 / 16 | ALO / SLO | Add / subtract to lower | Figs 3, 7 |
| 17 / 18 | AML / SML | Add / subtract absolute to lower | Figs 35-38 |
| 60 / 61 | RAU / RSU | Reset and add / subtract into upper | Figs 6, 10 |
| 65 / 66 | RAL / RSL | Reset and add / subtract into lower | Figs 4, 8 |
| 67 / 68 | RAM / RSM | Reset and add / subtract absolute into lower | Figs 35-38 |
| 19 | MPY | Multiply | Figs 11-12 |
| 14 | DIV | Divide, keep remainder | Fig.13 |
| 64 | DVR | Divide and reset upper | Fig.14 |
| 20 / 21 | STL / STU | Store lower / upper | Figs 16-17 |
| 22 / 23 | SDA / SIA | Store data / instruction address | Figs 18-19 |
| 24 | STD | Store distributor | Fig.31 |
| 69 | LDD | Load distributor | Fig.31 |
| 30 / 31 | SRT / SRD | Shift right / shift right and round | Figs 20-21 |
| 35 / 36 | SLT / SCT | Shift left / shift left and count | Figs 22-24 |
| 44 / 45 | NZU / NZE | Branch on non-zero upper / accumulator | Figs 26-27 |
| 46 | BMI | Branch on minus | Fig.28 |
| 47 | BOV | Branch on overflow (resets the indicator) | Fig.29 |
| 90-99 | BD0-BD9 | Branch on 8 in a distributor digit (9 = no branch, anything else stops) | Fig.30 |
| 84 | TLU | Table lookup | Figs 32-33 |
| 70 | RD1 | Read a card into words 1-10 of the D-address's band | OM p.25 |
| 71 | WR1 | Punch a card from words 27-36 of the D-address's band | OM p.26 |
| 72-78 | RC1, RD2-RC3 | Other units — stop: only the 533 on synchronizer 1 is attached | — |

Figures are the BOC register diagrams the tests reproduce.

## Cards and the 533

The machine has one 533 Card Read Punch on synchronizer 1. Cards are
first-class: a deck goes into the hopper, the operator presses Read
Start, and the program sees the cards only through RD.

**Deck files** are plain text, one card per line, 80 columns; a shorter
line has blank columns at the end, an empty line is a blank card. Each
column is one character in the usual Hollerith/EBCDIC correspondence.
Signs are zone punches over a word's units column (OM p.17), written the
way later zoned-decimal "signed overpunch" data writes them:

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 12 punch (plus) | `{` | `A` | `B` | `C` | `D` | `E` | `F` | `G` | `H` | `I` |
| 11 punch (minus) | `}` | `J` | `K` | `L` | `M` | `N` | `O` | `P` | `Q` | `R` |

A plain digit in the units column reads as plus; punching uses the 12
for plus, as the manual recommends for full validity checking.

**A fixed board instead of a plugboard.** The real 533 read and punched
through a wired control panel. Here every card is read the way a load
card is (OM p.46): columns 1-10 into word 1 (column 1 = high-order
digit), ..., columns 71-80 into word 8, words 9-10 = +0. Punching is the
mirror image: words 1-8 of the punch area to columns 1-80. A blank or
non-numeric column in a word is a read validity error (RVC stop).

**Load cards** carry a 12 punch in column 1 — the manual leaves the column
to the panel wiring ("a 12 punch in any specific column", OM p.46). A load
card is read without the validity check, and the RD then takes its next
instruction from its D-address instead of its I-address. That is how
decks load themselves, and how a program spots a control card without
testing for it.

**Read and punch areas.** RD copies the read buffer into words 1-10 of the
band its D-address is in (`70 0663` reads into 0651-0660); WR punches
words 27-36 (`71 0123` punches 0127-0136), leaving them unchanged.

**The read feed** (OM pp.20-25): hopper, first read station, second read
station, stacker. Read Start runs three cards in, the first card's data
landing in the read buffer, so the first RD only copies it and feeds the
next. When the hopper runs dry three unprocessed cards are still in the
feed; an RD then waits (the "card reader waiting" stop, which Program
Start resumes) until more cards arrive or the operator presses End of
File, which lets the program read the last cards. An RD after the last
card finds a blank buffer: a validity stop. A deck of three cards or fewer
needs End of File from the start.

**Timing** (BOC p.22). An RD or WR holds the program up for 61 ms (635
word times) while the mechanical cycle gets going, and a read cycle takes
300 ms (200 cards a minute), a punch cycle 600 ms; a second RD or WR
before its unit's cycle is over waits for it. The data moves when the
I/O area's first word comes round under the heads. (OM p.25 puts the
read interlock lower — "an average of 270 milliseconds is available for
computing" of the 300 — the emulator uses BOC's 61 ms.)

**The console bootstrap.** Computer Reset sets the address register to
8000; with `70 1951 9000` in the storage-entry switches, Program Start
reads the first card. If it is a load card, the machine branches to 1951
and the card's own instructions take over.

**Self-loading decks** (`--deck`, the console's `deck` and `boot`) use the
one-instruction-per-card load routine of Andree, *Programming the IBM
650* (1958), section 2.6 — the standard prepunched load card of the day:

| card word | columns | contents |
|---|---|---|
| 1 | 1-10 | `69 1954 1953` LDD: the card's word 4 to the distributor, next 1953 |
| 2 | 11-20 | blank (problem reference, if wanted) |
| 3 | 21-30 | `24 AAAA 8000` STD: distributor to drum address AAAA, next 8000 |
| 4 | 31-40 | the word to store |
| 5-8 | 41-80 | blank (comments) |

with the load punch over column 1. Each card goes into 1951-1960, runs
its own two instructions and returns to the switches for the next. A
transfer card, `00 0000 SSSS`, ends the deck and sends the machine to
the program; data cards may follow it. A card without the load punch
would send the RD to 9000 — a storage selection stop, the book's sign
of a card punched wrong. The read area 1951-1960 is overwritten by every
card, so a program to be loaded this way can't use it.

Simplified: punched cards land in the punch stacker at once (the real
punch feed holds the last two until it is run out); no read-validity
switch, no DPBC checking, no feed checks or restart procedures; the
synchronizer's buffer isn't a separate store (an RD reads the card at the
front of the feed directly).

### Notable behaviour

- **Multiply** runs the machine's own loop: shift left, use the digit
  shifted out as a counter, add the multiplicand that many times, with an
  extra count on a carry out of the top. This reproduces BOC Fig.12C,
  where a large lower half produces the manual's "incorrect answer".
- **Shift and count** writes its count into lower positions 2-1; a count
  of 10 signals overflow.
- **Error stops** (invalid op, storage selection, distributor digit,
  divide overflow, read validity) leave the address register on the
  failing instruction.

### Deliberate simplifications

- A zero add/subtract result keeps the accumulator's previous sign.
- Not modelled: the 2-word-time saving for arithmetic with D = 8001, and
  the exact timing of executing an instruction held in 8002/8003 (can be
  one word time off the chart).
- New machine: Programmed switch on STOP, Overflow switch on SENSE.
  Program Start resumes after HLT or overflow stops, and retries an RD
  waiting for cards; other stops need Computer Reset, which clears the
  program register, distributor, accumulator and error circuits and sets
  the address register to 8000 (GI p.14, OM p.54).
