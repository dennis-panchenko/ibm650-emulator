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
drum timing, the console, and (soon) working with card decks the way 650
programmers did. It is not a gate-level model of the hardware.

Written in C, step-driven (no blocking run loop), following the layout of
the [LGP-30 emulator](https://github.com/dennis-panchenko/lgp-30-emulator).

## Status

**Milestone 1 of 4 done: the core.** The full basic instruction set (every
operation in the IBM *Basic Operation Codes* bulletin except card I/O) with
word-time drum timing, as a library plus tests. Golden-vector tested against
the bulletin's own register diagrams, including its gross-pay program run
end to end and the multiply example where the manual shows the machine
producing an "incorrect answer".

Not there yet:

2. Console REPL — console keys, storage-entry switches, stepping, register
   lights, drum inspection, trace.
3. 533 Card Read Punch — decks, hopper and stackers, load cards, the console
   bootstrap, punching.
4. A simple assembler and sample programs.

SOAP II (the 650's own symbolic assembler, run on the emulator) is the
planned follow-up.

## Building

Requires a C11 compiler and `make`. No other dependencies.

```
make            # builds the core
make test       # builds and runs the test suite under ASan/UBSan
```

## How it works

[ABOUT.md](ABOUT.md) describes the machine as modelled, the timing model,
the instruction set with manual citations, and the module layout.

## Manuals

[bitsavers.org](https://bitsavers.org/pdf/ibm/650/) — mainly
*General Information* (24-5000-0), *Basic Operation Codes* (24-5002-0) and
*Card Reader and Punch* (24-5001-0).

## Contributing

Issues and PRs welcome, especially corrections — this was built by an
enthusiast reading 1950s documentation, not a subject-matter expert, so
mistakes are entirely possible. If you spot one, a PR with a manual page
citation is the fastest way to get it fixed.

## Authors

Dennis Panchenko, with Claude (Anthropic) as coding assistant and co-author.

## License

MIT — see [LICENSE](LICENSE).
