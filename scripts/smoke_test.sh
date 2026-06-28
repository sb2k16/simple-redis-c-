#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SERVER="$ROOT/bin/simple-redis"
CLIENT="$ROOT/bin/redis-client"
PORT=6389
ADDR="127.0.0.1:$PORT"
SNAP="$ROOT/data/test-dump.json"

cd "$ROOT"
make build

rm -f "$SNAP"
mkdir -p "$(dirname "$SNAP")"

"$SERVER" --port "$PORT" --snapshot "$SNAP" --max-keys 100 &
PID=$!

cleanup() {
  kill -INT "$PID" 2>/dev/null || true
  wait "$PID" 2>/dev/null || true
}
trap cleanup EXIT

sleep 0.5

run() {
  "$CLIENT" "$ADDR" "$@"
}

assert_contains() {
  local haystack="$1"
  local needle="$2"
  if [[ "$haystack" != *"$needle"* ]]; then
    echo "assert failed: expected '$needle' in output:"
    echo "$haystack"
    exit 1
  fi
}

out=$(run SET foo bar)
assert_contains "$out" OK
out=$(run GET foo)
assert_contains "$out" bar

run SET expkey temp >/dev/null
out=$(run EXPIRE expkey 2)
assert_contains "$out" 1
sleep 2
out=$(run GET expkey)
assert_contains "$out" "(nil)"

run DEL mylist >/dev/null || true
out=$(run LPUSH mylist a b c)
assert_contains "$out" 3
out=$(run LRANGE mylist 0 -1)
assert_contains "$out" c
out=$(run RPOP mylist)
assert_contains "$out" a

run DEL leaderboard >/dev/null || true
out=$(run ZADD leaderboard 10 alice)
assert_contains "$out" 1
out=$(run ZSCORE leaderboard alice)
assert_contains "$out" 10

OUT="$ROOT/data/pubsub.out"
rm -f "$OUT"
"$CLIENT" "$ADDR" SUBSCRIBE news > "$OUT" 2>&1 &
SUBPID=$!
sleep 0.5
out=$(run PUBLISH news "hello world")
assert_contains "$out" 1
sleep 0.5
kill "$SUBPID" 2>/dev/null || true
wait "$SUBPID" 2>/dev/null || true
assert_contains "$(cat "$OUT")" "hello world"

out=$(run SAVE)
assert_contains "$out" OK
test -f "$SNAP"

echo ""
echo "All smoke tests passed."
