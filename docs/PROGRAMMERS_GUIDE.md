# IBM 650 Programmer's Guide

How to program the IBM 650 on this emulator: the machine as a
programmer sees it, every instruction, drum timing and optimum
programming, the console, cards, and booting a program from a deck.

It assumes you can program something, but not that you have seen a 650.
You can learn the machine from this guide alone. IBM's manuals are cited
for anyone who wants to check a detail against the original:

- **GI**: *General Information* (24-5000-0)
- **BOC**: *Basic Operation Codes* (24-5002-0)
- **OM**: *650 Manual of Operation* (22-6060-2)
- **RP**: *Card Reader and Punch* (24-5001-0)
- **Andree**: R. V. Andree, *Programming the IBM 650 Magnetic Drum
  Computer and Data-Processing Machine* (1958)

All of them are on [bitsavers.org](https://bitsavers.org/pdf/ibm/650/).
[ABOUT.md](../ABOUT.md) covers how the emulator itself is built.

Every console session in this guide is real output, and `make test`
replays them (`tests/guide.sh`), so they stay true as the emulator
changes.

## Contents

1. [Getting started](#1-getting-started)
2. [The machine](#2-the-machine)
3. [Instructions](#3-instructions)
4. [Timing and optimum programming](#4-timing-and-optimum-programming)
5. [Programming techniques](#5-programming-techniques)
6. [Program files](#6-program-files)
7. [The console](#7-the-console)
8. [Cards and the 533](#8-cards-and-the-533)
9. [Booting from cards](#9-booting-from-cards)
10. [Batch mode](#10-batch-mode)
11. [Differences from the real 650](#11-differences-from-the-real-650)
12. [Appendix: quick reference](#12-appendix-quick-reference)

---

## 1. Getting started

Build the emulator (any C11 compiler and `make`; nothing else is needed):

```
make
```

Then start the console with one of the example programs.
`examples/chain_optimum.650` computes (A + B − C) × D with A = 12,
B = 25, C = 7 and D = 4:

```
$ ./bin/ibm650 examples/chain_optimum.650
loaded examples/chain_optimum.650; address register 0048
650> s
      rev  addr  instruction      . wait   I instruction   D data   = busy                      wt
     0.00  0048  RAU 0001 0055    ................................................I==D===       55
650> 
     1.10  0055  AUP 0008 0013    I==D====                                                       8
650> 
     1.26  0013  SUP 0016 0021    I==D====                                                       8
650> p
program register  1100160021+  SUP 0016 0021  (from 0013)
address register  0021
distributor       0000000007+
accumulator       0000000030 0000000000+   (8003 | 8002)
overflow          off
switches          entry 0000000000+ · programmed STOP · overflow SENSE
status            ready
drum              position 21 · 3 instr · 1.42 rev (0.007 s) · idle 0.96 rev (67.6%)
                  waiting for instructions 0.96 rev, for data 0.00 rev
533               hopper 0 · feed 0 · read stacker 0 · punched 0
650> f
HLT at 0058 · 6 instr · 2.24 rev (0.011 s) · idle 0.96 rev (42.9%)
650> m 0105 1
  0105  0000000120+
650> q
```

What happened:

- **`s`** stepped one instruction. An empty line repeats the step.
  Each trace line shows when the instruction began (in drum
  revolutions), its address, the instruction, and a *timing bar* with
  one character per 1/50 of a revolution. The first instruction waited
  48 word times for location 0048 to come round under the read heads.
  The next two found their instruction (`I`) and data (`D`) immediately.
  [Section 4](#4-timing-and-optimum-programming) explains the bar.
- **`p`** showed the console panel: the instruction just executed, the
  address of the next one, the registers, the switches and the counters.
  The accumulator holds 30, which is 12 + 25 − 7.
- **`f`** ran the program to the end as fast as the host allows. The
  status line says it stopped on a halt (`HLT`) at 0058, after 6
  instructions and 2.24 drum revolutions (11 ms of 650 time).
- **`m 0105 1`** showed one word of memory: the result, 120.

`?` lists every command. For line editing and command history, run the
console under [rlwrap](https://github.com/hanslub42/rlwrap):
`rlwrap ./bin/ibm650`.

---

## 2. The machine

The IBM 650 (1954) is a decimal, stored-program computer. Its memory is
a magnetic drum, and almost everything about programming it follows
from that.

### 2.1 Words

Memory, registers and cards all hold the same unit, the **word**: ten
decimal digits and a sign. There is no binary and there are no
characters. A word is either a signed number or an instruction,
depending only on how it is used.

This guide writes words the way the manuals do, all ten digits with the
sign after them: `0000000120+`, `6000010055+`. The digit positions are
numbered from the right, IBM style:

```
position   10  9   8  7  6  5   4  3  2  1   sign
digit       6  0   0  0  0  1   0  0  5  5    +
```

Things to know about numbers:

- Arithmetic is **sign and magnitude**, not complement. A word holds
  −9,999,999,999 to +9,999,999,999.
- **Minus zero exists** (`0000000000-`) and is a different word from
  plus zero, although the branch instructions treat both as zero.
- There is **no decimal point**. Where the point sits is the
  programmer's business, managed with the shift instructions.

### 2.2 The drum

The drum holds **2000 words** at addresses **0000-1999**. It turns at
12,500 rpm, so one revolution takes 4.8 ms. A word can be read or
written only while it passes under the heads.

The words are arranged in **40 bands of 50**. Address 0000 sits in
band 0 at drum position 0, 0001 at position 1, and so on: the position
of an address is its last two digits mod 50. Addresses 0000, 0050,
0100, … 1950 are all at position 0 and pass under their heads at the
same moment.

The time for one word to pass the heads is a **word time**: 0.096 ms, or
1/50 of a revolution. It is the emulator's unit of time. At word time
*t* the words at drum position *t* mod 50 are under the heads.

This means the time an instruction takes depends on **where its operand
and the next instruction are on the drum**. If the next word you need
has just gone past, you wait almost a whole revolution for it. That is
the subject of [section 4](#4-timing-and-optimum-programming).

### 2.3 Addresses

| Address | What it is | Read | Write |
|---|---|---|---|
| 0000-1999 | drum | yes | yes |
| 8000 | the console's storage-entry switches | yes | no (set from the console) |
| 8001 | the distributor | yes | no |
| 8002 | the lower half of the accumulator | yes | no |
| 8003 | the upper half of the accumulator | yes | no |

Any other address (2000-7999, 8004-9999) is invalid, and using one
stops the machine with a *storage selection error*. The store
instructions accept only drum addresses.

The four 8000-addresses can be used as data addresses, and even as
instruction addresses: `I` = 8002 makes the machine take its next
instruction from the lower accumulator (see
[section 5.4](#54-executing-a-computed-instruction)). 8000 and 8001 are
available at once; 8002 can be read only on even word times and 8003
only on odd ones.

### 2.4 Registers

**Accumulator** (8003 upper, 8002 lower). Twenty digits with a single
sign. Arithmetic treats it as one 20-digit number: adding to the lower
half carries into the upper half. Read separately, each half comes out
as a word with the accumulator's sign. The panel shows it as
`upper lower sign`.

After a divide that keeps its remainder, the halves have different
signs for a while: the quotient (lower) has the quotient's sign, and the
remainder (upper) has the dividend's. The panel then shows a
`remainder sign` line. The next multiply, divide or reset operation
ends this.

**Distributor** (8001). A one-word register that every word read from
or written to the drum passes through. Arithmetic instructions leave
their operand in it, store instructions leave the stored word in it,
and LDD/STD load and store it directly. You can use it as a scratch
word that costs no drum time (D = 8001).

**Program register.** Holds the instruction being executed. The panel
shows where it came from.

**Address register.** The address of the next instruction. Computer
Reset sets it to 8000.

**Overflow indicator.** Set when an add or subtract carries out of the
top of the accumulator, or when a shift-and-count can't bring a
significant digit to the top within its limit. BOV tests and clears
it.

### 2.5 The console

The real 650 had a control panel of switches and lamps. The emulator
keeps the parts a programmer uses (all set with the `k`, `c` and `g`
commands, [section 7](#7-the-console)):

- **Storage-entry switches** (address 8000): ten digits and a sign that
  the program can read like a word of storage. After Computer Reset, the
  machine's first instruction comes from here.
- **Programmed switch**: on STOP (the default), HLT stops the machine.
  On RUN, HLT does nothing.
- **Overflow switch**: on SENSE (the default), an overflow just sets
  the indicator for BOV to test. On STOP, it also stops the machine.
- **Computer Reset** (`c`) clears the accumulator, the distributor, the
  program register and the overflow indicator, and sets the address
  register to 8000. It does **not** touch the drum.
- **Program Start** (`r`, `f`, `s`, `w`) runs from the address register.
- **Transfer** (`g addr`) sets the address register, which is how you
  start a program anywhere.
- **Address stop** (`b addr`) stops the machine before it executes the
  instruction at that address. The real 650 had one; the emulator
  allows several.

The 650 has **no keyboard input and no printer**. A running program sees
only the drum, the storage-entry switches and cards. The console's
commands are for the operator.

### 2.6 Stops

| Stop | Cause | To continue |
|---|---|---|
| `HLT at nnnn` | an HLT with the Programmed switch on STOP | Program Start: carries on at HLT's I-address |
| `overflow stop after nnnn` | overflow with the Overflow switch on STOP | Program Start |
| `card reader waiting at nnnn` | an RD with no card ready ([8.4](#84-the-read-feed)) | add cards or press End of File, then Program Start |
| `divide check (quotient overflow)` | the quotient won't fit in ten digits ([3.4](#34-multiply-and-divide)) | Computer Reset |
| `storage selection error` | an invalid address in D or I, a store to a non-drum address, or a TLU that runs off the drum | Computer Reset |
| `invalid operation code` | an op code the machine doesn't have | Computer Reset |
| `branch-distributor digit neither 8 nor 9` | a BD instruction found another digit | Computer Reset |
| `no such input-output unit` | op codes 72-78 (units not attached here) | Computer Reset |
| `read validity check` | a card column that isn't a digit ([8.3](#83-reading-and-punching)) | Computer Reset |

Error stops leave the address register on the instruction that failed,
so `p` and `m` show you what went wrong. The console refuses to run
until you press Computer Reset (`c`), which sets the address register to
8000; use `g` to go back to your program.

---

## 3. Instructions

### 3.1 Format and the execution cycle

An instruction is a word in three fields:

```
OP     D-address   I-address
60     0001        0055          = 6000010055 = RAU 0001 0055
```

- **OP** (positions 10-9): the operation code, 00-99.
- **D** (positions 8-5): the *data address*, usually where the
  operand is.
- **I** (positions 4-1): the *instruction address*, where the **next
  instruction** is.

The sign is ignored.

Every instruction names its successor. There is no program counter
that steps on to the next location. A program can be scattered round
the drum in any order, and placing it well is the main skill of 650
programming.

The machine repeats one cycle:

1. Wait for the word at the address register to come under the heads,
   and read it into the program register.
2. Wait for the D-address to come round, if the operation needs it, and
   execute the operation.
3. Put the I-address (or, for a branch that is taken, the D-address)
   into the address register.

In this guide and in program files, instructions are written with their
SOAP mnemonic (SOAP was the 650's assembler): `RAU 0001 0055`. The
two-digit code works too: `60 0001 0055`.

### 3.2 No operation and stop

| Code | Mnemonic | Operation |
|---|---|---|
| 00 | NOP | Nothing. Go to I. D must still be a valid address. |
| 01 | HLT | Stop if the Programmed switch is on STOP; Program Start then continues at I. On RUN, the same as NOP. |

`HLT 0000 0058` at 0058 is the usual way to end: Program Start runs the
HLT again, and the machine stops again.

### 3.3 Add and subtract

Twelve instructions, all in the same pattern: the word at D goes to the
distributor and is added to (or subtracted from) the upper or lower
half of the accumulator.

| Code | Mnemonic | Operation |
|---|---|---|
| 10 | AUP | Add to upper |
| 11 | SUP | Subtract from upper |
| 15 | ALO | Add to lower |
| 16 | SLO | Subtract from lower |
| 17 | AML | Add absolute value to lower |
| 18 | SML | Subtract absolute value from lower |
| 60 | RAU | Reset accumulator, add to upper |
| 61 | RSU | Reset accumulator, subtract from upper |
| 65 | RAL | Reset accumulator, add to lower |
| 66 | RSL | Reset accumulator, subtract from lower |
| 67 | RAM | Reset accumulator, add absolute value to lower |
| 68 | RSM | Reset accumulator, subtract absolute value from lower |

- **Reset** sets the whole accumulator to plus zero first. RAL is the
  usual "load"; RAU loads the upper half, where a multiply wants its
  multiplier.
- **To upper** adds the word to positions 20-11 of the accumulator; the
  lower half is unchanged. **To lower** adds it to positions 10-1, with
  any carry going into the upper half.
- **Absolute** uses the word's magnitude, ignoring its sign.
- **Overflow**: a carry out of the top of the accumulator is lost and
  sets the overflow indicator.
- **Zero results** keep the accumulator's previous sign. So subtracting
  a number from itself can leave minus zero.
- **D = 8001** uses the distributor as it stands; D = 8002 or 8003 adds
  a half of the accumulator to itself.

### 3.4 Multiply and divide

| Code | Mnemonic | Operation |
|---|---|---|
| 19 | MPY | Multiply |
| 14 | DIV | Divide, keep the remainder |
| 64 | DVR | Divide and reset the remainder |

**MPY**: put the multiplier in the **upper** accumulator (usually with
RAU). The multiplicand is the word at D. The 20-digit product replaces
the whole accumulator, with the low-order ten digits in the lower half.
Products of ten-digit numbers fit, so MPY does not overflow. The lower
accumulator should be zero before a multiply. If it isn't, its digits
take part in the arithmetic and the result is wrong (BOC Fig.12C shows
this). The emulator reproduces the wrong answer.

**DIV / DVR**: the dividend is the whole 20-digit accumulator; the
divisor is the word at D. The quotient goes to the **lower** half. DIV
leaves the remainder in the upper half; DVR clears it. The quotient's
sign follows the usual rule; the remainder keeps the dividend's sign
(see [2.4](#24-registers)).

The quotient must fit in ten digits, which means the **upper half of the
dividend must be smaller than the divisor** (in magnitude). If not, or
if the divisor is zero, the machine stops with a *divide check*.

A ten-digit dividend goes in the lower half with the upper half zero
(RAL). To divide with more precision, put it in the upper half (RAU) and
the quotient comes out scaled by 10^10.

### 3.5 Store

| Code | Mnemonic | Operation |
|---|---|---|
| 20 | STL | Store lower accumulator at D |
| 21 | STU | Store upper accumulator at D |
| 22 | SDA | Store data address: see below |
| 23 | SIA | Store instruction address: see below |
| 24 | STD | Store the distributor at D |
| 69 | LDD | Load the distributor from D |

D must be a drum address (0000-1999). The stored word passes through the
distributor and is still there afterwards. STL and STU store the
accumulator's sign, except that STU straight after a DIV stores the
remainder's sign.

**SDA and SIA** change part of a word, for instruction modification
([section 5](#5-programming-techniques)). SDA replaces positions 8-5
(the D-address) of the **distributor** with positions 8-5 of the lower
accumulator, then stores the distributor at D. SIA does the same with
positions 4-1 (the I-address). The rest of the stored word, and its
sign, come from the distributor, not from the word at D. So get the
word you want to change into the distributor first (LDD it, or let an
add leave it there). BOC Figs. 18-19.

**LDD and STD** move a word without touching the accumulator.

### 3.6 Shifts

| Code | Mnemonic | Operation |
|---|---|---|
| 30 | SRT | Shift right |
| 31 | SRD | Shift right and round |
| 35 | SLT | Shift left |
| 36 | SCT | Shift left and count |

All four shift the whole 20-digit accumulator. The number of places is
the **units digit of the D-address** (`SRT 0003` shifts three places).
The other digits of D must still make a valid address. Vacated
positions fill with zeros, digits shifted off the end are lost, and the
sign does not change.

- **SRT** n: right n places; 0 means no shift.
- **SRD** n: right n places (0 means 10), then rounded on the last digit
  shifted out: 5 or more adds 1 to the magnitude.
- **SLT** n: left n places; 0 means no shift. No overflow.
- **SCT** n: shift left until the high-order digit (position 20) is not
  zero, but at most n places (0 means 10). Then it puts a count into
  positions 2-1 of the lower accumulator. With n = 0 the count is the
  number of shifts taken. With n ≠ 0 it is 10 − n plus the shifts. If
  the high-order digit was already non-zero the count is 00. If the
  limit runs out before a significant digit reaches position 20, the
  count is 10 and the overflow indicator is set. SCT is used to
  normalise numbers, for example in floating-point routines. BOC
  Figs. 22-24.

### 3.7 Branches

Branches test a condition and go to **D if it is true**, to **I if it is
false**.

| Code | Mnemonic | Goes to D when |
|---|---|---|
| 44 | NZU | the upper accumulator is not zero |
| 45 | NZE | the accumulator (either half) is not zero |
| 46 | BMI | the accumulator's sign is minus |
| 47 | BOV | the overflow indicator is on (and turns it off) |
| 90-99 | BD0-BD9 | a digit of the distributor is 8 |

- Plus zero and minus zero are both zero to NZU and NZE. BMI looks only
  at the sign, so minus zero branches.
- **BD0-BD9** test one digit of the distributor: BD1-BD9 test positions
  1-9 and BD0 position 10. An 8 branches to D, a 9 goes to I, and any
  other digit stops the machine. Programmers kept a word of 8s and 9s
  as a set of switches.

There is no unconditional branch instruction: every instruction already
says where to go next. To "jump", just point the I-address there.

### 3.8 Table lookup

| Code | Mnemonic | Operation |
|---|---|---|
| 84 | TLU | Table lookup |

TLU searches a table of arguments for the first one whose magnitude is
**greater than or equal to** the magnitude of the distributor.

- The search starts at the **first word of D's band** (the address with
  its last two digits rounded down to 00 or 50). It runs through each
  band's first 48 words, skipping the last two (positions 48 and 49),
  and on into the following bands.
- When it finds the argument, it puts D plus the match's distance from
  the start of the band into positions 8-5 of the lower accumulator.
  With D at the start of the band, that is the match's own address.
  Nothing else changes.
- If the search reaches 2000 without a match, the machine stops (storage
  selection).

The usual pattern: load the search key into the distributor (LDD), TLU
into a table whose arguments are in ascending order, then use the
address in the lower accumulator to fetch the function value from a
parallel table. Because only the D-address part of the lower half
changes, an instruction already in the lower accumulator gets its D
filled in (see [5.4](#54-executing-a-computed-instruction)). BOC
Figs. 32-33.

### 3.9 Card input and output

| Code | Mnemonic | Operation |
|---|---|---|
| 70 | RD1 | Read a card into the read area of D's band |
| 71 | WR1 | Punch a card from the punch area of D's band |
| 72-78 | RC1, RD2, WR2, RC2, RD3, WR3, RC3 | Other card units: not attached (machine stops) |

The read area is words **1-10 of the band** containing D: `RD1 1300`,
`RD1 1301` and `RD1 1349` all read into 1301-1310, and `RD1 1351` reads
into 1351-1360. The punch area is words **27-36** of the band:
`WR1 1377` punches from 1377-1386.

A card fills words 1-8 of the area, and words 9-10 are set to zero. A
**load card** makes RD continue at D instead of I.
[Section 8](#8-cards-and-the-533) has everything else: the card format,
the feed, load cards and timing.

---

## 4. Timing and optimum programming

### 4.1 How long an instruction takes

An instruction's time is mostly *waiting*. The emulator counts word
times exactly, applying IBM's **Optimum Program Chart** (BOC p.23):

1. The instruction is read when its address comes under the heads. Call
   that word time *n*. Because there are 50 words per revolution, *n*
   mod 50 is the instruction's drum position.
2. The data word can be read **no earlier than** a fixed number of word
   times after *n* (for most instructions, n + 3). If the data address
   has just gone past, the machine waits for it to come round.
3. The next instruction can be read **no earlier than** a fixed number
   of word times after the data was read (or after *n*, for
   instructions with no data). Again, if it has just gone past, the
   machine waits.

So an instruction at position *p* that reads its data at position
*p* + 3, and finds the next instruction just at the earliest time it
can be read, wastes nothing. Put either one word too early and it costs
nearly a revolution.

### 4.2 The timing table

*n* is the word time the instruction was read; *d* is the word time the
data was read. "even/odd" is the parity of *n* (or of *d*, where *d* is
used), which is the same as the parity of the drum position.

| Instructions | Data read at or after | Next instruction at or after |
|---|---|---|
| NOP, HLT | none | n + 4 |
| AUP SUP ALO SLO AML SML RAU RSU RAL RSL RAM RSM | n + 3 | d + 5 even, d + 4 odd; +2 if a non-reset add changes the accumulator's sign |
| MPY | n + 3 | d + 21 even, d + 20 odd, + 2 × the number of additions the multiply performed |
| DIV, DVR | n + 3 | d + 61 even, d + 60 odd, + 2 × the sum of the quotient digits |
| STL | n + 5 even, n + 4 odd | d + 3 |
| STU | n + 4 even, n + 5 odd | d + 3 |
| SDA, SIA | n + 3 even, n + 4 odd | d + 3 |
| STD | n + 3 | d + 3 |
| LDD | n + 3 | d + 3 |
| NZU | none | D: n + 3 even, n + 4 odd. I: n + 4 even, n + 5 odd |
| NZE | none | D: n + 4 even, n + 3 odd. I: n + 5 even, n + 4 odd |
| BMI | none | D: n + 3. I: n + 4 |
| BOV | none | D: n + 3. I: n + 5 |
| BD1-BD8 | none | D: n + 3. I: n + 5 |
| BD0, BD9 | none | D: n + 4. I: n + 5 |
| SRT, SLT, SCT | none | read from n + 7 even, n + 6 odd; starts at n + 2s + 3 even, n + 2s + 2 odd (s = places shifted; no shift: n + 6 even, n + 5 odd) |
| SRD | none | read from n + 7 even, n + 6 odd; starts at n + 2s + 5 even, n + 2s + 4 odd |
| TLU | the band's first word at or after n + 3, then one word per word time | the match's time + 5 even, + 6 odd |
| RD1, WR1 | the area's first word, at or after n + 3 and after the unit's previous cycle | 635 word times (61 ms) after that earliest time |

D = 8000 or 8001 is available immediately; 8002 only on even word
times; 8003 only on odd.

A shift lets the next instruction be *read* early but doesn't let it
*start* until the shift is done; the trace bar shows the hold-up as
`=`.

### 4.3 Reading the trace bar

Each trace line (from `s`, `w`, or `t` with `r`/`f`) has a bar with one
character per word time. It starts when the machine became ready for
this instruction and ends when it is ready for the next:

| Mark | Meaning |
|---|---|
| `.` | waiting for the drum |
| `I` | the instruction is read |
| `D` | the data is read (or written) |
| `=` | busy: executing, or held by a shift or card interlock |

Dots **before** the `I` mean the instruction was placed badly: the
previous instruction's I-address pointed at a word that had just gone
past. Dots **between** `I` and `D` mean the data was placed badly. The
number after the bar is the total in word times. A bar longer than 60
word times is cut off with `...`.

The status line after a stop gives totals: instructions, elapsed
revolutions (with the time in 650 seconds), and how much of that was
*idle*, waiting for the drum. `p` splits the idle time into instruction
waits and data waits. `z` sets the counters to zero.

### 4.4 Example: the same program, placed two ways

`examples/chain_sequential.650` has the (A + B − C) × D program in
consecutive locations, the way you'd write it for a modern machine:

```
$ ./bin/ibm650 --run --trace examples/chain_sequential.650
      rev  addr  instruction      . wait   I instruction   D data   = busy                      wt
     0.00  0000  RAU 0010 0001    I==.......D====                                               15
     0.30  0001  AUP 0011 0002    ....................................I==.......D===            50
     1.30  0002  SUP 0012 0003    .....................................I==.......D====          52
     2.34  0003  MPY 0013 0004    ....................................I==.......D==========...  72
     3.78  0004  STL 0014 0005    ...............I====.....D==                                  28
     4.34  0005  HLT 0000 0005    ......................................I===                    42
HLT at 0005 · 6 instr · 5.18 rev (0.025 s) · idle 3.90 rev (75.3%)
program register  0100000005+  HLT 0000 0005  (from 0005)
address register  0005
distributor       0000000120+
accumulator       0000000000 0000000120+   (8003 | 8002)
overflow          off
switches          entry 0000000000+ · programmed STOP · overflow SENSE
status            programmed stop
drum              position 09 · 6 instr · 5.18 rev (0.025 s) · idle 3.90 rev (75.3%)
                  waiting for instructions 3.24 rev, for data 0.66 rev
533               hopper 0 · feed 0 · read stacker 0 · punched 0
```

RAU at 0000 reads its data at 0010 and is ready for its next instruction
at about position 15. But that instruction is at 0001, which passed 14
word times ago, so the machine waits most of a revolution. Every
instruction does the same: 75% of the time is spent waiting.

`examples/chain_optimum.650` is the same program placed for the drum
(BOC Fig.41):

```
$ ./bin/ibm650 --run --trace examples/chain_optimum.650
      rev  addr  instruction      . wait   I instruction   D data   = busy                      wt
     0.00  0048  RAU 0001 0055    ................................................I==D===       55
     1.10  0055  AUP 0008 0013    I==D====                                                       8
     1.26  0013  SUP 0016 0021    I==D====                                                       8
     1.42  0021  MPY 0024 0051    I==D==========================                                30
     2.02  0051  STL 0105 0058    I===D==                                                        7
     2.16  0058  HLT 0000 0058    I===                                                           4
HLT at 0058 · 6 instr · 2.24 rev (0.011 s) · idle 0.96 rev (42.9%)
program register  0100000058+  HLT 0000 0058  (from 0058)
address register  0058
distributor       0000000120+
accumulator       0000000000 0000000120+   (8003 | 8002)
overflow          off
switches          entry 0000000000+ · programmed STOP · overflow SENSE
status            programmed stop
drum              position 12 · 6 instr · 2.24 rev (0.011 s) · idle 0.96 rev (42.9%)
                  waiting for instructions 0.96 rev, for data 0.00 rev
533               hopper 0 · feed 0 · read stacker 0 · punched 0
```

Here is how the placement was worked out, using the table above. RAU at
0048 (position 48, even) reads its data no earlier than n + 3 =
position 51 = position 1, so A is at 0001. The data is read at an odd
time, so the next instruction can come at d + 4 = position 5, which
is 0055. AUP at 0055 wants its data at position 8 (0008) and its
successor at 8 + 5 = 13 (0013). And so on. The only wait left is the
first one, for 0048 to come round at the start.

To try a slowed-down version, run either program in walk mode (`w`):
the drum turns once every 2 seconds and the bars grow as you watch.

### 4.5 The drum map

`v` shows the whole drum, one row per band in use and one column per
drum position. It shows how a program is spread round the drum:

```
$ ./bin/ibm650 examples/chain_optimum.650
loaded examples/chain_optimum.650; address register 0048
650> f
HLT at 0058 · 6 instr · 2.24 rev (0.011 s) · idle 0.96 rev (42.9%)
650> v
      0         1         2         3         4         
      01234567890123456789012345678901234567890123456789
0000  .r......r....i..r....i..r.......................i.
0050  .i...i..i.........................................
0100  .....w............................................
      i executed  w written  r read  : other non-zero  . zero
650> q
```

`v 0100` shows only the band containing 0100.

### 4.6 Rules of thumb

These are the rules 650 programmers used, which the table in 4.2 makes
exact:

- Put an add's operand **3 words** after the instruction, and the next
  instruction **4-5 words** after the operand.
- Put a store's target **4-5 words** after the instruction, and the next
  instruction **3 words** after that.
- A taken branch can go **3-4 words** on; the fall-through goes
  **4-5 words** on.
- Constants used in several places can only be in the right place for
  one of them. Duplicate a constant if that saves a revolution.
- Within one band, positions repeat every 50 words, so "3 words after
  0048" is 0001 (or 0051, 0101, …).

`examples/count_sequential.650` and `count_optimum.650` (Andree,
Problem Set 3-11) are the same counting loop placed both ways. In a
minute of 650 time, one adds 6,251 times and the other 75,001.

Exercise: `examples/table_sum.650` ([section 5](#5-programming-techniques))
is written one instruction after another and spends 87% of its time
waiting. Use the table to place it for the drum, and watch the idle
figure drop.

---

## 5. Programming techniques

The 650 has no index registers, no subroutine call and no indirect
addressing. Programs get the same effects by treating instructions as
data.

### 5.1 Loops and instruction modification

An instruction is a number, so adding 0000010000 to it adds 1 to its
D-address. `examples/table_sum.650` sums a table that way:

```
0010  RAL 0250 0011     ; the sum so far
0011  ALO 0200 0012     ; + the next table entry (D-address changes)
0012  STL 0250 0013     ; store the sum
0013  RAL 0011 0014     ; the ALO instruction itself
0014  ALO 0051 0015     ; + 1 in the D-address
0015  STL 0011 0016     ; put it back
0016  SLO 0052 0017     ; compare with the ALO for 0210, one past the end
0017  NZE 0010 0018     ; not equal yet: round again
0018  HLT 0000 0018

0051  0000010000+       ; 1 in the D-address positions (8-5)
0052  1502100012+       ; ALO 0210 0012: the loop is done
```

The loop ends when the modified instruction equals a constant: the
subtraction gives zero, and NZE falls through to I. Because the
program changes itself, it sets the instruction up before the loop
(0000-0003 in the file) so that it can be run again.

```
$ ./bin/ibm650 examples/table_sum.650
loaded examples/table_sum.650; address register 0000
650> f
HLT at 0018 · 85 instr · 97.44 rev (0.468 s) · idle 85.22 rev (87.5%)
650> m 0011 1
  0011  1502100012+  ALO 0210 0012
650> m 0250 1
  0250  0000000592+
650> q
```

The ALO has moved to 0210, and the sum is in 0250.

SDA and SIA do the same job when the new address is computed rather than
counted: compute it into positions 8-5 (or 4-1) of the lower
accumulator, LDD the instruction, then SDA (or SIA) it back.

### 5.2 Constants

There are no immediate operands: every constant is a word on the drum,
and each one is an extra placement decision. Common ones: 1 in the
units position (`0000000001+`), 1 in the D-address
(`0000010000+`), 1 in the I-address (`0000000001+` again, since
positions 4-1 are the units). A word of 8s and 9s serves as a set of
flags for the BD instructions.

### 5.3 Subroutines

There is no call instruction. To call a subroutine, the caller puts a
**return instruction** in the accumulator (an instruction whose
I-address is where to go back to, often `NOP 0000 back`) and branches
to the subroutine. The subroutine stores that word as its own last
instruction (STL) and later runs into it. Or the caller leaves just the
return address in positions 4-1 of the lower accumulator, and the
subroutine loads its exit instruction into the distributor (LDD) and
stores it back with SIA, changing only its I-address.

### 5.4 Executing a computed instruction

An instruction's I-address can be 8002 or 8003: the next instruction
is then the contents of the lower or upper accumulator. A program can
build an instruction arithmetically and execute it without storing it.
After a TLU, the lower accumulator holds the found address in positions
8-5. If the rest of the lower half already held an instruction (say
`RAL 0000 nnnn`, loaded before the lookup), branching to 8002 executes
it with the found address as its D-address. Adding a constant to the
lower half first moves that D-address on to a parallel table of
function values.

The distributor (8001) works the same way.

---

## 6. Program files

A `.650` file is a hand-written image of the drum: one word per line,
each with its address. The emulator loads it straight onto the drum.
There are no labels or symbols; that is the assembler's job (SOAP II is
the project's next goal).

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

Each line is one of:

| Line | Meaning |
|---|---|
| `NNNN OP D I` | an instruction at drum address NNNN. OP is a mnemonic (any case) or a two-digit code; D and I are 1-4 digits. |
| `NNNN word` | a number: 1-10 digits with an optional `+` or `-` after them (`25+`, `4000-`, `1502100012`). Without a sign it is plus. |
| `start NNNN` | where the program starts (optional) |
| blank | ignored |

- `;` starts a comment, anywhere on a line.
- The order of lines doesn't matter, but each address may appear only
  once.
- Words not mentioned are zero.
- Any op code 00-99 can be written as two digits, including ones the
  machine doesn't have (running them stops the machine).

**Loading** (`l file`, or the file on the command line) clears the
drum, does a Computer Reset, zeroes the counters, and sets the address
register to `start`. Without a `start` line the address register stays
at 8000, and the program begins the authentic way: with the instruction
in the storage-entry switches (`k entry 0000000048` would give a
`NOP 0000 0048` that branches to 0048, for instance).

**Errors** give the file, line and column, and leave the drum as it was:

```
prog.650:2:6: unknown operation 'RAX'
prog.650:7:1: address already used on line 3
```

---

## 7. The console

The console is a plain line-oriented command prompt, `650>`. The
commands you use most are one letter. Numbers are decimal; addresses are
1-4 digits (`48` is 0048). When standard input is not a terminal (a
file or a pipe), the console echoes each command after the prompt, so a
transcript reads like a session.

### 7.1 Running

| Command | Does |
|---|---|
| `s [n]` | Step one instruction (or n), with a trace line for each. An empty line repeats it. |
| `r` | Program Start at the 650's own speed: 4.8 ms per revolution, paced against the real clock. |
| `f` | Run as fast as the host can, about 10,000 times the real speed. |
| `w [seconds]` | Walk: the drum slowed to one revolution every 2 seconds (or the given number), with each trace bar growing as the drum turns. The setting is remembered. |

Any run stops when the machine stops, at an address stop, or when you
press **Enter** or **Ctrl-C**. If you type a command instead of just
Enter, the run stops and the command runs. After the stop, a status line
gives the reason and the counters.

After an error stop, the run commands refuse to start until you press
Computer Reset (`c`):

```
machine stopped (invalid operation code at 0012) -- c for computer reset
```

### 7.2 Looking

| Command | Shows |
|---|---|
| `p` | The panel: program register, address register, distributor, accumulator, overflow, switches, status, drum position, counters, the 533. |
| `p b` | The same, with each register digit also drawn as its bi-quinary lamps (two "bi" lamps for 0 or 5, five "quinary" lamps for 0-4), as on the real console. |
| `m addr [n]` | n words (default 10) from addr. `m` alone continues where the last one stopped; an empty line does too. |
| `m addr = word` | Change a drum word, or the storage-entry switches (8000): `m 0107 = 25+`. |
| `v [addr]` | The drum map ([4.5](#45-the-drum-map)); with addr, just that band. |
| `t` | Trace on/off: a trace line for every instruction during `r` and `f`. |
| `d` | Timing bar in trace lines on/off. |

A memory line looks like this:

```
* >0048  6000010055+  RAU 0001 0055
```

`*` marks an address stop and `>` the address register. A word is shown
as an instruction if it has been executed, or if it looks like one (a
known op code other than 00, and valid D and I addresses).

### 7.3 Console switches and keys

| Command | Does |
|---|---|
| `k` | Show the storage-entry switches and the Programmed and Overflow switches. |
| `k entry word` | Set the storage-entry switches: `k entry 7019519000`. |
| `k prog stop` / `k prog run` | Programmed switch: HLT stops, or HLT is ignored. |
| `k ovfl sense` / `k ovfl stop` | Overflow switch: just set the indicator, or stop too. |
| `c` | Computer Reset ([2.5](#25-the-console)). |
| `g addr` | Transfer: set the address register. |
| `b addr` | Set or clear an address stop on a drum address; `b` alone lists them. The machine stops *before* executing that address (but not on the first instruction of a run, so you can continue from it). |
| `z` | Zero the counters. |

```
$ ./bin/ibm650 examples/table_sum.650
loaded examples/table_sum.650; address register 0000
650> b 0018
address stop set at 0018
650> f
address stop at 0018 · 84 instr · 96.42 rev (0.463 s) · idle 84.28 rev (87.4%)
650> k entry 7019519000
storage entry 7019519000+ · programmed STOP · overflow SENSE
650> c
computer reset; address register 8000
650> g 0000
address register 0000
650> q
```

### 7.4 Cards

| Command | Does |
|---|---|
| `hopper deck...` | Put decks in the read hopper (behind any cards already there) and press Read Start. |
| `hopper` | Show the 533: cards in the hopper, the feed, the read stacker, the punch stacker. |
| `hopper clear` | Take the cards out of the hopper and run the feed out to the stacker. |
| `eof` | Press End of File: let the program read the last cards in the feed. |
| `punch [n]` | Show the last n punched cards (default 10). |
| `punch save file` / `punch clear` | Save the punched cards as a deck file / empty the punch stacker. |
| `stacker [n]` / `stacker clear` | The last n cards read / empty the read stacker. |
| `boot deck [data...]` | Boot a self-loading deck ([section 9](#9-booting-from-cards)). |
| `deck file` | Write the loaded `.650` program as a self-loading deck. |

### 7.5 Program files and leaving

| Command | Does |
|---|---|
| `l [file]` | Load a `.650` file; `l` alone reloads the current one. |
| `e [file]` | Edit the file in `$VISUAL` or `$EDITOR` (or `vi`), then reload it. |
| `?` | Help. |
| `q` | Quit. |

---

## 8. Cards and the 533

Cards were the 650's only way in and out. The emulator has one **IBM 533
Card Read Punch**: a reader and a punch, attached through
*synchronizer 1*, which is why the instructions are RD1 and WR1.

### 8.1 Deck files

A deck is a text file, **one card per line**, 80 columns. A shorter line
has blank columns at the end, an empty line is a blank card, and lines
longer than 80 characters are an error. There are no comments: every
line is a card.

A column holds a digit, a blank, or a digit with a **sign punch** (a
"zone" punch in row 12 for plus or row 11 for minus) over it. The sign
goes over the **last digit of each word**. Sign-punched digits are
written as the letters a card printer would show for them:

| | 0 | 1 | 2 | 3 | 4 | 5 | 6 | 7 | 8 | 9 |
|---|---|---|---|---|---|---|---|---|---|---|
| 12 punch (plus) | `{` | `A` | `B` | `C` | `D` | `E` | `F` | `G` | `H` | `I` |
| 11 punch (minus) | `}` | `J` | `K` | `L` | `M` | `N` | `O` | `P` | `Q` | `R` |

A plain digit in a word's last column reads as plus. The punch always
writes the 12 punch for plus. So `000000039G` is +397 and `000000039P`
is −397.

### 8.2 The card layout

The real 533 was programmed with a wired control panel. The emulator
has one fixed wiring, the standard one:

```
columns   1-10   11-20   21-30   31-40   41-50   51-60   61-70   71-80
word        1      2       3       4       5       6       7       8
```

Column 1 is a word's high-order digit and column 10 its units digit.

### 8.3 Reading and punching

**RD1 D** copies the card in the read buffer to words 1-10 of D's band
(the read area: words 1-8 from the card, 9 and 10 set to plus zero) and
moves the next card into the buffer. A **blank or non-digit column**
stops the machine with a *read validity check*, naming the column.

**WR1 D** punches words 27-34 of D's band (the punch area, words 27-36;
only the first 8 fit on a card) onto a new card in the punch stacker.
The words stay on the drum.

The read and punch areas are fixed per band:

| D in band | read area | punch area |
|---|---|---|
| 0000-0049 | 0001-0010 | 0027-0036 |
| 0050-0099 | 0051-0060 | 0077-0086 |
| … | … | … |
| 1950-1999 | 1951-1960 | 1977-1986 |

**Load cards.** A card with a 12 punch in **column 1** is a load card.
It is read without the validity check (non-digit columns read as 0),
and the RD then takes its next instruction from **D instead of I**. A
program usually reads into the area and continues at I; a load card
makes it jump into the area it has just read, and so run the card's
own instructions. That is how decks load themselves
([section 9](#9-booting-from-cards)). It is also how a program can
recognise a control card at the end of its data without testing for it.

### 8.4 The read feed

The 533 holds three cards between the hopper and the read stacker:

```
hopper → [3rd] → [2nd] → [buffer] → read stacker
```

- `hopper deck` puts cards in the hopper and presses **Read Start**,
  which runs three cards into the feed. The first card's data is then
  waiting in the buffer, and the first RD copies it at once.
- Each RD takes the card in the buffer and feeds the next one in.
- When the hopper runs dry, three unread cards are still in the feed.
  An RD then stops the machine with **card reader waiting**: put more
  cards in (`hopper`), or press **End of File** (`eof`) to let the
  program read the last three. Program Start (`r`) carries on.
- An RD after the last card finds a blank buffer: a read validity
  check. That is the normal end of many 650 jobs.

A deck of three cards or fewer needs `eof` from the start.

### 8.5 Timing

| | |
|---|---|
| Read cycle | 300 ms (200 cards a minute) |
| Punch cycle | 600 ms (100 cards a minute) |
| Interlock after RD or WR | 61 ms (635 word times) before the next instruction |

An RD or WR waits for its unit's previous cycle to finish. So a program
can compute for about 240 ms between reads at full card speed; if it
reads more often, it waits for the reader.

### 8.6 Example: a card job

`examples/formula.650` is Andree's section 3-10 example. For each data
card it computes T = (A + B) × C / D and punches A, B, C, D and T:

```
$ ./bin/ibm650 examples/formula.650
loaded examples/formula.650; address register 0101
650> hopper examples/formula.cards
examples/formula.cards: 8 cards
hopper 5 · feed 3 · read stacker 0 · punched 0
650> f
card reader waiting at 0101 (hopper empty -- hopper <deck> or eof, then r) · 56 instr · 537.02 rev (2.578 s) · idle 392.66 rev (73.1%)
650> eof
end of file: the program can read the last cards (r to continue)
hopper 0 · feed 3 · read stacker 5 · punched 5 · END OF FILE
650> f
read validity check at 0101: card column 1 · 90 instr · 913.02 rev (4.382 s) · idle 682.58 rev (74.8%)
650> punch 3
punch stacker: 8 cards
        ....+....1....+....2....+....3....+....4....+....5....+....6....+....7....+....8
     6  000942227G000000272G000000035C000000357B000000000{000093141H000000000{000000000{
     7  000396423I000007744H000000098H000002738C000000000{000014582G000000000{000000000{
     8  000239295H000003680A000000006I000009666F000000000{000000173D000000000{000000000{
650> q
```

The program is a loop with no exit. It reads until the cards run out
and ends on the validity check, the usual way. The counters show the
drum idle 75% of the time: card machines are slow, and a program like
this one is *card-bound*.

`punch save results.cards` would keep the output. A punched deck can be
put back in the hopper and read, like a real one.

---

## 9. Booting from cards

A finished 650 program lived on cards. To run it, the operator:

1. put the deck in the hopper (data cards behind it),
2. set the storage-entry switches to a read instruction,
   `70 1951 9000`,
3. pressed Computer Reset, Program Start, and Read Start.

Computer Reset makes 8000 the first address, so the machine executes
the switches: `RD1 1951` reads the first card into 1951-1960. That card
is a load card, so the machine continues at D = 1951, executing the
card's own words.

### 9.1 The self-loading deck

`deck` (or `--deck`) writes a loaded `.650` program as such a deck, in
the one-word-per-card format of Andree's section 2.6 (the standard load
card of the day). Every card loads one word:

| Card word | Columns | Contents |
|---|---|---|
| 1 | 1-10 | `69 1954 1953`: LDD the card's word 4, next instruction 1953. Load punch over column 1. |
| 2 | 11-20 | blank |
| 3 | 21-30 | `24 AAAA 8000`: STD to address AAAA, next instruction 8000 (the switches: read the next card) |
| 4 | 31-40 | the word to load |
| 5-8 | 41-80 | blank |

The deck has one card for each word in the `.650` file, in address
order, then a
**transfer card**, `00 0000 SSSS` with the load punch: a NOP that sends
the machine to the program's start address. Data cards can follow.

Rules that follow from this:

- The program needs a `start` line.
- It can't use 1951-1960: every card is read there.
- A card without the load punch sends the RD to its I-address, 9000: a
  storage selection stop, the book's sign that a card is out of place.

### 9.2 Booting

`boot` does the operator's job: it runs out any cards still in the
feed, puts the decks in the hopper, sets the switches to `70 1951 9000`,
presses Computer Reset, zeroes the counters, and runs at native speed,
pressing End of File whenever the reader waits. It doesn't clear the
drum (Computer Reset never did).

```
$ ./bin/ibm650 --deck formula.deck examples/formula.650
formula.deck: 12 cards; boot with the storage-entry switches at 70 1951 9000
$ ./bin/ibm650
650> boot formula.deck examples/formula.cards
formula.deck: 12 cards
examples/formula.cards: 8 cards
storage entry 70 1951 9000, computer reset, program start
(end of file pressed)
read validity check at 0101: card column 1 · 125 instr · 1663.02 rev (7.982 s) · idle 1285.30 rev (77.3%)
650> punch 2
punch stacker: 8 cards
        ....+....1....+....2....+....3....+....4....+....5....+....6....+....7....+....8
     7  000396423I000007744H000000098H000002738C000000000{000014582G000000000{000000000{
     8  000239295H000003680A000000006I000009666F000000000{000000173D000000000{000000000{
650> q
```

The emulator started with an empty drum and loaded the program from the
cards. Then the program read the data cards behind it and punched the
same results as before. Loading 12 cards took about 4 seconds of 650
time.

The first cards of `formula.deck`:

```
F91954195C          240101800{701351010B
F91954195C          240102800{601351010F
```

`F` in column 1 is a 6 with a 12 punch (the load punch); `C` and `{` are
sign-punched 3 and 0, the plus signs of words 1 and 3; `B` and `F` end
word 4, which is the loaded instruction (`RD1 1351 0102` and
`RAU 1351 0106`).

---

## 10. Batch mode

`--run` runs a program without the console, reports how it stopped,
prints the panel, and exits:

```
$ ./bin/ibm650 --run examples/grosspay.650
HLT at 0126 · 8 instr · 6.60 rev (0.032 s) · idle 4.52 rev (68.5%)
program register  0100000126+  HLT 0000 0126  (from 0126)
address register  0126
distributor       0000000397+
accumulator       0000000000 0000085355+   (8003 | 8002)
overflow          off
switches          entry 0000000000+ · programmed STOP · overflow SENSE
status            programmed stop
drum              position 30 · 8 instr · 6.60 rev (0.032 s) · idle 4.52 rev (68.5%)
                  waiting for instructions 2.36 rev, for data 2.16 rev
533               hopper 0 · feed 0 · read stacker 0 · punched 0
```

| Option | |
|---|---|
| `--run` | Batch mode. |
| `--trace` | A trace line per instruction. |
| `--max-rev N` | Give up after N drum revolutions (default 100,000, about 8 minutes of 650 time). |
| `--hopper DECK` | Put a deck in the hopper; repeat for more. End of File is pressed whenever the reader runs dry. |
| `--punch FILE` | Save the punched cards. |
| `--entry WORD` | Set the storage-entry switches. Without a program file, the run starts from them: the console bootstrap. |
| `--deck DECK prog.650` | Write a self-loading deck and exit (not with `--run`). |

Runs at full host speed (like `f`). The exit status is **0** on HLT,
**1** on any other stop or on giving up, and **2** if a file couldn't be
read.

```
./bin/ibm650 --run --hopper examples/payroll.cards --punch pay.cards examples/payroll.650
./bin/ibm650 --run --entry 7019519000 --hopper formula.deck --hopper examples/formula.cards
```

---

## 11. Differences from the real 650

The emulator aims to be faithful wherever a programmer could tell the
difference, and simplifies below that.

- **Only the basic machine.** No 653 extensions (index registers,
  floating point, immediate-access core), no tapes, no RAMAC disk, no
  alphabetic cards. Only the 533 on synchronizer 1; RC1 and units 2-3
  stop the machine.
- **A fixed card layout** instead of a wired control panel: 8 words per
  card, sign over each word's last column, load punch in column 1.
- **Card handling** is simplified: punched cards reach the stacker at
  once, and there are no feed checks, restart procedures, read-validity
  switch, or double-punch/blank-column checking.
- **Storage is decimal**, not bi-quinary; the code shows only in the
  panel's lamps (`p b`).
- **Timing** is exact to the word time for everything in the Optimum
  Program Chart. Not modelled: the 2-word-time saving for arithmetic
  with D = 8001, and the exact timing of an instruction executed from
  8002/8003 (it can be one word time off).
- A **zero result** of an add or subtract keeps the accumulator's
  previous sign.
- A **new machine** starts with the Programmed switch on STOP and the
  Overflow switch on SENSE, and its drum all zeros.

[ABOUT.md](../ABOUT.md) lists the modelling decisions with the manual
pages behind them.

---

## 12. Appendix: quick reference

### Operation codes

| Code | Mnemonic | Operation | Section |
|---|---|---|---|
| 00 | NOP | No operation | [3.2](#32-no-operation-and-stop) |
| 01 | HLT | Stop | [3.2](#32-no-operation-and-stop) |
| 10 | AUP | Add to upper | [3.3](#33-add-and-subtract) |
| 11 | SUP | Subtract from upper | [3.3](#33-add-and-subtract) |
| 14 | DIV | Divide | [3.4](#34-multiply-and-divide) |
| 15 | ALO | Add to lower | [3.3](#33-add-and-subtract) |
| 16 | SLO | Subtract from lower | [3.3](#33-add-and-subtract) |
| 17 | AML | Add absolute to lower | [3.3](#33-add-and-subtract) |
| 18 | SML | Subtract absolute from lower | [3.3](#33-add-and-subtract) |
| 19 | MPY | Multiply | [3.4](#34-multiply-and-divide) |
| 20 | STL | Store lower | [3.5](#35-store) |
| 21 | STU | Store upper | [3.5](#35-store) |
| 22 | SDA | Store data address | [3.5](#35-store) |
| 23 | SIA | Store instruction address | [3.5](#35-store) |
| 24 | STD | Store distributor | [3.5](#35-store) |
| 30 | SRT | Shift right | [3.6](#36-shifts) |
| 31 | SRD | Shift right and round | [3.6](#36-shifts) |
| 35 | SLT | Shift left | [3.6](#36-shifts) |
| 36 | SCT | Shift left and count | [3.6](#36-shifts) |
| 44 | NZU | Branch on non-zero upper | [3.7](#37-branches) |
| 45 | NZE | Branch on non-zero accumulator | [3.7](#37-branches) |
| 46 | BMI | Branch on minus | [3.7](#37-branches) |
| 47 | BOV | Branch on overflow | [3.7](#37-branches) |
| 60 | RAU | Reset and add into upper | [3.3](#33-add-and-subtract) |
| 61 | RSU | Reset and subtract into upper | [3.3](#33-add-and-subtract) |
| 64 | DVR | Divide and reset | [3.4](#34-multiply-and-divide) |
| 65 | RAL | Reset and add into lower | [3.3](#33-add-and-subtract) |
| 66 | RSL | Reset and subtract into lower | [3.3](#33-add-and-subtract) |
| 67 | RAM | Reset and add absolute into lower | [3.3](#33-add-and-subtract) |
| 68 | RSM | Reset and subtract absolute into lower | [3.3](#33-add-and-subtract) |
| 69 | LDD | Load distributor | [3.5](#35-store) |
| 70 | RD1 | Read a card | [3.9](#39-card-input-and-output) |
| 71 | WR1 | Punch a card | [3.9](#39-card-input-and-output) |
| 72-78 | RC1 … RC3 | Other card units (not attached) | [3.9](#39-card-input-and-output) |
| 84 | TLU | Table lookup | [3.8](#38-table-lookup) |
| 90-99 | BD0-BD9 | Branch on 8 in a distributor digit | [3.7](#37-branches) |

### Console commands

```
s [n]   step          r  run (650 speed)    f  run fast       w [s]  walk
p [b]   panel         m addr [n] / m addr = word               v [addr]  drum map
t       trace on/off  d  timing bar on/off
b addr  address stop  g addr  transfer      c  computer reset  z  zero counters
k       switches: k entry <word>, k prog stop|run, k ovfl stop|sense
hopper [deck...] / hopper clear     eof     punch [n|save f|clear]
stacker [n|clear]   boot deck [data...]     deck file
l [file]  load        e [file]  edit        ?  help            q  quit
```

### Numbers to remember

| | |
|---|---|
| Word | 10 digits + sign |
| Drum | 2000 words, 40 bands × 50 positions |
| Word time | 0.096 ms |
| Revolution | 50 word times, 4.8 ms |
| Read area | words 1-10 of the band (x01-x10, x51-x60) |
| Punch area | words 27-36 of the band (x27-x36, x77-x86) |
| Card reader | 200 cards a minute |
| Card punch | 100 cards a minute |
| Bootstrap | storage-entry switches `70 1951 9000`, Computer Reset, Program Start |
