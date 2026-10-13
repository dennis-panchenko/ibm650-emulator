# IBM 650 Emulator

## Overview

This document covers the emulator's architecture: the machine as modelled,
the timing model, the instruction set with manual citations, and the module
layout. See [README.md](README.md) for build instructions.

Manual references: **GI** = *General Information* (24-5000-0), **BOC** =
*Basic Operation Codes* (24-5002-0). Test names in `tests/` cite the BOC
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
              inspection API
tests/
  test_types.c, test_accum.c, test_cpu.c — BOC register-diagram vectors
  test_timing.c — Optimum Program Chart and the BOC pp.24-29 examples
```

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
| 70-78 | RD/WR/RC | Card I/O — stops with "card I/O not available" until milestone 3 | — |

Figures are the BOC register diagrams the tests reproduce.

### Notable behaviour

- **Multiply** runs the machine's own loop: shift left, use the digit
  shifted out as a counter, add the multiplicand that many times, with an
  extra count on a carry out of the top. This reproduces BOC Fig.12C,
  where a large lower half produces the manual's "incorrect answer".
- **Shift and count** writes its count into lower positions 2-1; a count
  of 10 signals overflow.
- **Error stops** (invalid op, storage selection, distributor digit,
  divide overflow) leave the address register on the failing instruction.

### Deliberate simplifications

- A zero add/subtract result keeps the accumulator's previous sign.
- Not modelled: the 2-word-time saving for arithmetic with D = 8001, and
  the exact timing of executing an instruction held in 8002/8003 (can be
  one word time off the chart).
- New machine: Programmed switch on STOP, Overflow switch on SENSE.
  Program Start resumes after HLT or overflow stops; other stops need
  Computer Reset, which sets the address register to 8000 (GI p.14).
