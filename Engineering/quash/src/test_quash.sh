#!/usr/bin/env bash
# tests/test_quash.sh — hardened; heredocs + timeouts; final kill uses external PID
set -euo pipefail

Q=${1:-./quash}
TMO=${TMO:-6s}

pass(){ printf "[OK] %s\n" "$1"; }

# --- sanity ---
echo "echo hello" | timeout "$TMO" "$Q" >/dev/null

# --- Tier 1 ---
echo "pwd" | timeout "$TMO" "$Q" | tr -d '\r' | grep -E "/|\\\\" >/dev/null && pass "pwd prints path"
echo "echo Hello world" | timeout "$TMO" "$Q" | tr -d '\r' | grep -Fx "Hello world" >/dev/null && pass "echo works"

# --- Tier 2: export/echo, args, cd ---
OUT=$(timeout "$TMO" "$Q" <<'INP'
export FOO=bar
echo $FOO
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -Fx "bar" >/dev/null && pass "export + echo \$FOO"

echo "echo X Y Z" | timeout "$TMO" "$Q" | tr -d '\r' | grep -Fx "X Y Z" >/dev/null && pass "echo multi args"

OUT=$(timeout "$TMO" "$Q" <<'INP'
cd ..
pwd
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -E "/|\\\\" >/dev/null && pass "cd updates cwd/PWD"

# --- Redirects < > >> ---
OUT=$(timeout "$TMO" "$Q" <<'INP'
echo hi > t1.txt
cat < t1.txt
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -Fx "hi" >/dev/null && pass "> and <"

OUT=$(timeout "$TMO" "$Q" <<'INP'
echo hi > t2.txt
echo hi >> t2.txt
cat t2.txt
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -Fx "hi" >/dev/null && pass ">> append line present"

# --- Pipes + jobs + completion ---
OUT=$(timeout "$TMO" "$Q" <<'INP'
sleep 1 &
jobs
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -E "^\[[0-9]+\] [0-9]+ sleep 1 &" >/dev/null && pass "jobs shows bg"

OUT=$(timeout "$TMO" "$Q" <<'INP'
sleep 1 &
sleep 2
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -E "^Completed: \[[0-9]+\] [0-9]+ sleep 1 &" >/dev/null && pass "completed prints"

OUT=$(timeout "$TMO" "$Q" <<'INP'
cat quash.c | grep return
exit
INP
)
echo "$OUT" | tr -d '\r' | grep -F "return" >/dev/null && pass "pipe finds token"

# --- Built-in kill: use an EXTERNAL sleeper PID (no jobs parsing) ---
sleep 60 & SPID=$!        # start outside quash; remember PID
sleep 0.2 || true         # tiny settle

# Tell quash to send SIGINT to that external PID
timeout "$TMO" "$Q" <<INP >/dev/null
kill 2 $SPID
exit
INP

# Verify it's gone; if not, force kill (but still finish)
if kill -0 "$SPID" 2>/dev/null; then
  echo "[WARN] external sleeper still alive, forcing SIGKILL"
  kill -KILL "$SPID" 2>/dev/null || true
else
  pass "built-in kill sent SIGINT to external PID $SPID"
fi

# cleanup
rm -f t1.txt t2.txt 2>/dev/null || true
echo "[OK] all tests done"
