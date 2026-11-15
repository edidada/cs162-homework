#!/bin/bash
set -euo pipefail
mode=$1
cd lab-rpc
./bin/server > "../evidence/server-$mode.txt" 2>&1 &
server=$!
trap 'kill "$server" 2>/dev/null || true; wait "$server" 2>/dev/null || true' EXIT
for i in $(seq 1 50); do if rpcinfo -t localhost 0x20000001 1 >/dev/null 2>&1; then break; fi; sleep .1; done
/tmp/rpc-test | tee "../evidence/runtime-$mode.txt"
pids=()
for i in $(seq 1 8); do /tmp/rpc-large > "../evidence/large-$mode-$i.txt" 2>&1 & pids+=("$!"); done
for pid in "${pids[@]}"; do wait "$pid"; done
kill -0 "$server"
if grep -E 'ERROR: AddressSanitizer|runtime error:' "../evidence/server-$mode.txt"; then exit 1; fi
echo "8 independent TCP clients completed 5 message sizes each ($mode)"
