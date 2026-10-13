#!/bin/sh
# Checks the sessions shown in docs/PROGRAMMERS_GUIDE.md. Every fenced
# block whose first line is "$ ./bin/ibm650 ..." is replayed: each "$ "
# line is run (in a scratch directory with bin/ and examples/ linked in),
# fed that segment's "650> " lines as commands -- the console echoes them
# back when stdin isn't a terminal -- and its output must match the
# block. Whitespace differences are ignored. Usage: tests/guide.sh <ibm650>
bin=${1:-bin/ibm650}
guide=docs/PROGRAMMERS_GUIDE.md
work=build/guide
status=0

rm -rf "$work"
mkdir -p "$work/bin" "$work/segments"
ln -s "$(cd "$(dirname "$bin")" && pwd)/$(basename "$bin")" "$work/bin/ibm650"
ln -s "$(pwd)/examples" "$work/examples"

awk -v dir="$work/segments" '
    function open_segment(cmd) {
        seg++
        base = sprintf("%s/%03d", dir, seg)
        print cmd > (base ".cmd")
        print block_line > (base ".line")
        printf "" > (base ".in")
        printf "" > (base ".want")
    }
    /^```/ {
        if (infence) { infence = 0; check = 0 } else { infence = 1; first = 1 }
        next
    }
    infence && first {
        first = 0
        check = index($0, "$ ./bin/ibm650") == 1
        block_line = NR
    }
    infence && check {
        if (substr($0, 1, 2) == "$ ") {
            open_segment(substr($0, 3))
            next
        }
        print > (base ".want")
        if (substr($0, 1, 5) == "650> ") {
            print substr($0, 6) > (base ".in")
        }
    }
' "$guide"

count=0
for cmd in "$work"/segments/*.cmd; do
    [ -f "$cmd" ] || continue
    base=${cmd%.cmd}
    line=$(cat "$base.line")
    run=$(cat "$cmd")
    count=$((count + 1))
    (cd "$work" && sh -c "$run" < "segments/$(basename "$base").in" \
        > "segments/$(basename "$base").got" 2>&1)
    if diff -b "$base.want" "$base.got" > "$base.diff"; then
        :
    else
        echo "FAIL: $guide:$line: $run"
        cat "$base.diff"
        status=1
    fi
done
[ $status -eq 0 ] && echo "PASS: $guide: $count sessions match"
exit $status
