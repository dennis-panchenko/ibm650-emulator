#!/bin/sh
# Runs every example in batch mode and checks its results, then checks
# that each one booted from its own self-loading deck (--deck) stops the
# same way and punches the same cards. Usage: tests/examples.sh <ibm650>
bin=${1:-bin/ibm650}
out=build/examples
mkdir -p "$out"
status=0

pass() { echo "PASS: $1"; }
fail() { echo "FAIL: $1"; status=1; }

# How each example ends, and the run's first line (counters cut off).
# A program's data deck is <name>.cards next to it.
stop_line() {
    "$@" | head -n 1 | sed 's/ ·.*//'
}
expected_stop() {
    case $1 in
    chain_optimum) echo "HLT at 0058" ;;
    chain_sequential) echo "HLT at 0005" ;;
    grosspay) echo "HLT at 0126" ;;
    payroll) echo "HLT at 0200" ;;
    formula) echo "read validity check at 0101, card column 1" ;;
    count_*) echo "gave up after 100 revolutions" ;;
    *) echo "(no expected stop for $1)" ;;
    esac
}

for f in examples/*.650; do
    name=$(basename "$f" .650)
    data=examples/$name.cards
    [ -f "$data" ] || data=
    hop=${data:+--hopper $data}
    want=$(expected_stop "$name")
    case $name in count_*) rev=100 ;; *) rev=100000 ;; esac
    got=$(stop_line "$bin" --run --max-rev $rev $hop --punch "$out/$name.punch1" "$f")
    case $got in
    "$want"*) pass "$f: $want" ;;
    *) fail "$f: expected '$want', got '$got'" ;;
    esac
    # Booted from its deck. A program that never stops would stop in a
    # different place (loading takes time), so only the punches count.
    "$bin" --deck "$out/$name.deck" "$f" > /dev/null || { fail "$f: --deck"; continue; }
    boot=$(stop_line "$bin" --run --max-rev 100000 --entry 7019519000 --hopper "$out/$name.deck" \
        $hop --punch "$out/$name.punch2")
    case $name in count_*) boot=$want ;; esac
    if [ "${got%%,*}" = "${boot%%,*}" ] && cmp -s "$out/$name.punch1" "$out/$name.punch2"; then
        pass "$f booted from its deck: same stop, same punches"
    else
        fail "$f booted from its deck: '$boot'"
    fi
done

"$bin" --run examples/grosspay.650 | grep -q '0000000000 0000085355+' \
    && pass "grosspay.650 gives 85355" || fail "grosspay.650 result"

grep -q '^205830000{000000039G000008535E' "$out/payroll.punch1" \
    && [ "$(wc -l < "$out/payroll.punch1")" -eq 4 ] \
    && pass "payroll punches 4 pay cards" || fail "payroll punches"

# T = (A + B) x C / D, worked out by hand for the first and last cards.
head -n 1 "$out/formula.punch1" | grep -q '^000526643I000008706F000000070A000002218G.\{10\}000016914D' \
    && tail -n 1 "$out/formula.punch1" | grep -q '^.\{50\}000000173D' \
    && [ "$(wc -l < "$out/formula.punch1")" -eq 8 ] \
    && pass "formula punches 8 result cards" || fail "formula punches"

# Andree's minute on each counting loop: the totals in the upper accumulator.
"$bin" --run --max-rev 12500 examples/count_sequential.650 | grep -q 'accumulator *0000006251 ' \
    && "$bin" --run --max-rev 12500 examples/count_optimum.650 | grep -q 'accumulator *0000075001 ' \
    && pass "count loops: 6251 vs 75001 in a minute" || fail "count loops"

"$bin" --run --entry 7019519000 --hopper examples/chain_selfload.cards \
    | grep -q '0000000000 0000000120+' \
    && pass "chain_selfload.cards boots from the console, gives 120" \
    || fail "chain_selfload.cards"
"$bin" --deck "$out/chain.cards" examples/chain_optimum.650 > /dev/null \
    && cmp -s "$out/chain.cards" examples/chain_selfload.cards \
    && pass "--deck chain_optimum.650 gives chain_selfload.cards" \
    || fail "--deck chain_optimum.650"

exit $status
