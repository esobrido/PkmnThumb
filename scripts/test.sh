#!/usr/bin/env bash
# Host tests: unit tests for every game module, then the readable scenarios.
#
#   scripts/test.sh          # everything
#   scripts/test.sh unit     # only tests/unit/*.c
#   scripts/test.sh scen     # only tests/scenarios/*.txt
#
# No Mote engine and no emulator are involved: the modules under game/src are
# pure logic (arrays in, arrays out), so they compile with plain gcc. game.c is
# the Mote glue and is deliberately not tested here — it is exercised by
# scripts/shot.sh (real module, real frames).
#
# Module paths map to test file names by replacing "/" with "_":
#   game/src/ow/map.c  ->  tests/unit/test_ow_map.c
set -uo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
CC="${CC:-gcc}"
CFLAGS="-std=c11 -O1 -g -Wall -Wextra -Werror -I$ROOT/game/src -I$ROOT/tests/unit"
OUT="$ROOT/tests/build"
WHAT="${1:-all}"

mkdir -p "$OUT"

modules=()
while IFS= read -r f; do
    modules+=("$f")
done < <(find "$ROOT/game/src" -name '*.c' -not -path '*/gen/*' -not -name 'game.c' | sort)

test_name_for() {                       # game/src/ow/map.c -> test_ow_map
    local rel="${1#"$ROOT"/game/src/}"
    printf 'test_%s' "$(printf '%s' "${rel%.c}" | tr '/' '_')"
}

fails=0
passed=0
fail() { printf '  FAIL %s\n' "$1"; fails=$((fails + 1)); }

# --- every module must have a unit test ------------------------------------
if [ "$WHAT" = "all" ] || [ "$WHAT" = "unit" ]; then
    for f in "${modules[@]}"; do
        want="$ROOT/tests/unit/$(test_name_for "$f").c"
        if [ ! -f "$want" ]; then
            fail "game/src/${f#"$ROOT"/game/src/} has no tests/unit/$(test_name_for "$f").c"
        fi
    done
fi

# --- unit tests -------------------------------------------------------------
if [ "$WHAT" = "all" ] || [ "$WHAT" = "unit" ]; then
    echo "== unit tests"
    for t in "$ROOT"/tests/unit/test_*.c; do
        [ -e "$t" ] || continue
        name="$(basename "$t" .c)"
        if ! "$CC" $CFLAGS "$t" "${modules[@]}" -lm -o "$OUT/$name" 2>"$OUT/$name.build.log"; then
            fail "$name does not build (see $OUT/$name.build.log)"
            continue
        fi
        if "$OUT/$name"; then
            passed=$((passed + 1))
        else
            fail "$name"
        fi
    done
fi

# --- scenarios --------------------------------------------------------------
if [ "$WHAT" = "all" ] || [ "$WHAT" = "scen" ]; then
    echo "== scenarios"
    if ! "$CC" $CFLAGS "$ROOT/tests/scenario_main.c" "${modules[@]}" -lm \
            -o "$OUT/scenario" 2>"$OUT/scenario.build.log"; then
        fail "scenario runner does not build (see $OUT/scenario.build.log)"
    else
        for s in "$ROOT"/tests/scenarios/*.txt; do
            [ -e "$s" ] || continue
            if "$OUT/scenario" "$s"; then
                passed=$((passed + 1))
            else
                fail "$(basename "$s")"
            fi
        done
    fi
fi

echo
if [ "$fails" -ne 0 ]; then
    echo "FAILED: $fails problem(s), $passed passed"
    exit 1
fi
echo "OK: $passed passed"
