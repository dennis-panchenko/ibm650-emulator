# IBM 650 Emulator

A from-scratch emulator of the IBM 650 Magnetic Drum Data-Processing Machine
(1954) — a decimal computer with 2000 ten-digit words on a drum spinning at
12,500 RPM. Every instruction names both its data address and the address of
the *next* instruction, so the programmer decides where on the drum each
instruction sits. Placing them so each one arrives under the heads just as
it's needed — "optimum programming" — could make a program several times
faster. This project models the drum word time by word time so that payoff
is visible, not just the opcodes.

The goal is the programmer's view of the machine: the instruction set, the
drum timing, the console, and working with card decks the way 650
programmers did. It is not a gate-level model of the hardware.

Written in C, step-driven (no blocking run loop), following the layout of
the [LGP-30 emulator](https://github.com/dennis-panchenko/lgp-30-emulator).

## What it looks like

The same calculation, (A + B − C) × D, run twice in the console's walk
mode. Each line is one instruction; its bar shows where the time went, one
character per word time (1/50 of a drum revolution).

Laid out one after another, the way you'd write it for a modern machine:

```
650> w
      rev  addr  instruction      . wait   I instruction   D data   = busy                      wt
     0.00  0000  RAU 0010 0001    I==.......D====                                               15
     0.30  0001  AUP 0011 0002    ....................................I==.......D===            50
     1.30  0002  SUP 0012 0003    .....................................I==.......D====          52
     2.34  0003  MPY 0013 0004    ....................................I==.......D==========...  72
     3.78  0004  STL 0014 0005    ...............I====.....D==                                  28
     4.34  0005  HLT 0000 0005    ......................................I===                    42
HLT at 0005 · 6 instr · 5.18 rev (0.025 s) · idle 3.90 rev (75.3%)
```

Each instruction finishes just after the next one has passed the heads,
so the machine sits out most of a revolution, every time.

Placed for the drum — IBM's "optimum programming" (BasicOpcodes Fig.41):

```
650> w
      rev  addr  instruction      . wait   I instruction   D data   = busy                      wt
     0.00  0048  RAU 0001 0055    ................................................I==D===       55
     1.10  0055  AUP 0008 0013    I==D====                                                       8
     1.26  0013  SUP 0016 0021    I==D====                                                       8
     1.42  0021  MPY 0024 0051    I==D==========================                                30
     2.02  0051  STL 0105 0058    I===D==                                                        7
     2.16  0058  HLT 0000 0058    I===                                                           4
HLT at 0058 · 6 instr · 2.24 rev (0.011 s) · idle 0.96 rev (42.9%)
```

Every instruction and every operand arrives under the heads just as it's
needed. The only wait left is at the start, for the drum to bring 0048
round. Same instructions, same answer, less than half the time — and on
the 650, where you put an instruction was half of programming it.

## Status

**Milestones 1-4 of 4 done.**

1. The core: the full basic instruction set (every operation in the IBM
   *Basic Operation Codes* bulletin) with word-time drum timing.
   Golden-vector tested against the bulletin's own register diagrams,
   including its gross-pay program run end to end and the multiply example
   where the manual shows the machine producing an "incorrect answer".
2. A console REPL and hand-written `.650` program files.
3. The 533 Card Read Punch: deck files, hopper and stackers, the three-card
   run-in, End of File, load cards, the console bootstrap, punching, and
   the reader's and punch's mechanical timing.

4. Turning `.650` programs into self-loading card decks, booted from
   the console; sample programs from Andree's *Programming the IBM 650*
   (1958).

SOAP II (the 650's own symbolic assembler, run on the emulator) is the
planned follow-up.

## Building

Requires a C11 compiler and `make`. No other dependencies.

```
make            # builds bin/ibm650
make test       # builds and runs the test suite under ASan/UBSan
```

## Usage

```
./bin/ibm650 examples/chain_optimum.650          # the console
./bin/ibm650 --run examples/grosspay.650         # run, report, exit
```

The console is a plain command line. The commands you use most are one
letter and Enter:

```
650> s              step one instruction (Enter again steps again)
650> r              run at the 650's own speed; Enter stops it
650> f              run as fast as possible
650> w              walk: the drum slowed to one turn every 2 seconds
650> p              the console lights: registers, switches, counters
650> m 0048 10      look at ten drum words
650> v              drum map: where the program sits around the drum
650> e              edit the program in $EDITOR, then reload it
650> hopper x.cards  put a deck in the card reader and press Read Start
650> eof            End of File: let the program read the last cards
650> punch          the punched cards (punch save out.cards to keep them)
650> deck x.cards   write the loaded program as a self-loading deck
650> boot x.cards   boot a deck from the console, as the operator did
```

`?` lists everything. For command history and line editing, run it under
[rlwrap](https://github.com/hanslub42/rlwrap) — a small standard tool
(`apt install rlwrap`, `brew install rlwrap`) that adds them to any
command-line program: `rlwrap ./bin/ibm650`.

## Programs

A `.650` file is a hand-written drum image: one word per line, with its
drum address always spelled out — on the 650, where you put an instruction
is half of programming it.

```
; (A + B - C) x D
start 0048
0048  RAU 0001 0055     ; A to the upper accumulator, next instruction at 0055
0055  AUP 0008 0013     ; + B
0013  SUP 0016 0021     ; - C
0021  MPY 0024 0051     ; x D
0051  STL 0105 0058     ; store the product
0058  HLT 0000 0058
0001  12+               ; data words: digits and an optional sign
0008  25+
0016  7+
0024  4+
```

Operations are written with their SOAP mnemonics or two-digit codes.
Without `start`, the program begins the authentic way: from the instruction
set up in the console's storage-entry switches (`k entry ...`). See
`examples/`.

The emulated programs, like the real ones, never read the keyboard — the
650 had none. They see only the drum and cards.

## Cards

A deck is a text file, one card per line, 80 columns. Numbers are punched
ten columns to a word, eight words to a card, with the sign as a zone
punch over the word's last digit — written as a letter, the way the card
would print it: `A`-`I` and `{` are plus 1-9 and 0, `J`-`R` and `}` minus.
A plain last digit counts as plus.

```
20583000000000000397000000000000000000000000000000000000000000000000000000000000
{00000020{
```

The first card is a time card: employee 20583, 39.7 hours. The second has
a 12 punch in column 1 (`{`), which makes it a *load card*: reading it
branches the program to the instruction it carries — here, to the end of
the job.

`examples/payroll.650` runs the bulletin's gross-pay calculation over a
deck of time cards and punches a pay card per employee:

```
$ ./bin/ibm650 examples/payroll.650
650> hopper examples/payroll.cards
examples/payroll.cards: 5 cards
hopper 2 · feed 3 · read stacker 0 · punched 0
650> r
card reader waiting at 0150 (hopper empty -- hopper <deck> or eof, then r) · ...
650> eof
650> r
HLT at 0200 · 60 instr · 427.08 rev (2.050 s) · idle 308.20 rev (72.2%)
650> punch
punch stacker: 4 cards
        ....+....1....+....2....+....3....+....4....+....5....+....6....+....7....+....8
     1  205830000{000000039G000008535E000000000{000000000{000000000{000000000{000000000{
     ...
```

As on the real 533, the last three cards are still in the feed when the
hopper runs dry, and the operator presses End of File to let the program
read them. The run is card-bound: the reader manages 200 cards a minute
and the punch 100, and the counters show the drum idling while it waits.

## Self-loading decks

A finished 650 program lived on cards, not in a file, and the machine
started from nothing but cards: Computer Reset, a read instruction in
the storage-entry switches, Program Start. `deck` (or `--deck`) turns a
`.650` file into such a deck, one word per card, each card carrying its
own two-instruction loader — the standard load card of the day, from
Andree's *Programming the IBM 650* (1958). A transfer card at the end
starts the program, and data cards can follow it in the hopper:

```
$ ./bin/ibm650 --deck formula.deck examples/formula.650
formula.deck: 12 cards; boot with the storage-entry switches at 70 1951 9000
$ ./bin/ibm650
650> boot formula.deck examples/formula.cards
formula.deck: 12 cards
examples/formula.cards: 8 cards
storage entry 70 1951 9000, computer reset, program start
(end of file pressed)
read validity check at 0101: card column 1 · 125 instr · 1663.02 rev (7.982 s) · ...
650> punch
```

`boot` does by hand what the operator did: deck in the hopper, `k entry
7019519000`, `c`, `r`, and `eof` when the reader waits for the last
cards. The program here — Andree's (A + B) × C / D example — loops over
its data cards until the reader runs out, the usual way a 650 job ended.

Batch mode takes the same: `--hopper deck` (repeatable; End of File is
pressed whenever the reader runs dry), `--punch out.cards`, `--entry
word`.

```
./bin/ibm650 --run --hopper examples/payroll.cards --punch pay.cards examples/payroll.650
./bin/ibm650 --run --entry 7019519000 --hopper examples/chain_selfload.cards
```

## Examples

| file | what it shows |
|---|---|
| `chain_sequential.650`, `chain_optimum.650` | the same arithmetic laid out naively and for the drum (BOC Fig.41): 5.2 vs 2.2 revolutions |
| `count_sequential.650`, `count_optimum.650` | Andree's Problem 3-11.1: "let each run for 1 minute" — 6,251 adds against 75,001 |
| `grosspay.650` | the gross-pay program of BOC Fig.33 |
| `payroll.650` + `payroll.cards` | gross pay over a deck of time cards, punching pay cards |
| `formula.650` + `formula.cards` | Andree's section 3-10: T = (A + B) × C / D for each data card, punched |
| `chain_selfload.cards` | `chain_optimum.650` as a self-loading deck |

[ABOUT.md](ABOUT.md#cards-and-the-533) has the details: the fixed card
layout used in place of a wired control panel, the read feed, timing.

## How it works

[ABOUT.md](ABOUT.md) describes the machine as modelled, the timing model,
the instruction set with manual citations, and the module layout.

## Manuals

[bitsavers.org](https://bitsavers.org/pdf/ibm/650/) — mainly
*General Information* (24-5000-0), *Basic Operation Codes* (24-5002-0),
*Card Reader and Punch* (24-5001-0) and the *Manual of Operation*
(22-6060-2) — and R. V. Andree, *Programming the IBM 650 Magnetic Drum
Computer and Data-Processing Machine* (1958), the textbook the sample
programs and the load-card format come from.

## Contributing

Issues and PRs welcome, especially corrections — this was built by an
enthusiast reading 1950s documentation, not a subject-matter expert, so
mistakes are entirely possible. If you spot one, a PR with a manual page
citation is the fastest way to get it fixed.

## Authors

Dennis Panchenko, with Claude (Anthropic) as coding assistant and co-author.

## License

MIT — see [LICENSE](LICENSE).
