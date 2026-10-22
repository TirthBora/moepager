#!/usr/bin/env bash
# Phase 7 go/no-go experiments A–D for one model (docs/RUNBOOK_GO_NO_GO.md).
#
#   MODEL=~/models/olmoe-1b-7b-0924-q4_k_m.gguf LLAMA_BIN=~/src/llama.cpp/build/bin \
#   BUDGETS="1500M 2500M" bash bench/phase7.sh
#
# Env:
#   MODEL       GGUF file (required)
#   LLAMA_BIN   dir with llama-bench (required for D)
#   MOE_TRACE   moe_trace binary (default build/moe_trace/moe_trace)
#   NTOK        decode tokens per ground-truth trace (default 256)
#   BUDGETS     cgroup MemoryMax values for B and D (default "1500M 2500M")
#   TG          llama-bench generated tokens (default 32)
#   STEPS       subset of "A B C D" (default all)
#   OUT         results dir (default bench/results/phase7-<model>-<date>)
set -euo pipefail
root="$(cd "$(dirname "$0")/.." && pwd)"
: "${MODEL:?set MODEL}"
B="$root/target/release/moepager"; D="$root/target/release/moepagerd"
MOE_TRACE="${MOE_TRACE:-$root/build/moe_trace/moe_trace}"
NTOK="${NTOK:-256}"; BUDGETS="${BUDGETS:-1500M 2500M}"; TG="${TG:-32}"; STEPS="${STEPS:-A B C D}"
name="$(basename "$MODEL" .gguf)"
OUT="${OUT:-$root/bench/results/phase7-$name-$(date +%Y%m%d-%H%M)}"
mkdir -p "$OUT"; exec > >(tee -a "$OUT/log.txt") 2>&1
want() { [[ " $STEPS " == *" $1 "* ]]; }
cold() { "$B" fault-io --drop "$MODEL" > /dev/null; sync; sleep "${SETTLE_S:-5}"; }

echo "== environment"
{ uname -r; lscpu | grep -E "Model name|^CPU\(s\)"; free -m | head -2;
  df -T "$MODEL" | tail -1; for f in /sys/class/bdi/*/read_ahead_kb; do echo "$f $(cat "$f")"; done;
  cat /sys/kernel/mm/lru_gen/enabled 2>/dev/null; git -C "${LLAMA_SRC:-$HOME/src/llama.cpp}" log -1 --format='llama.cpp %h %cd' 2>/dev/null; } | tee "$OUT/env.txt"
"$B" gguf-map "$MODEL" -o "$OUT/map.json" | tee "$OUT/map.txt"

prompts=(
  "chat|Hi! I'm planning a week-long trip to Japan in spring. Can you suggest an itinerary, what to pack, and some etiquette tips?"
  "code|Write a Python function that parses a CSV file of transactions, groups them by month and category, and prints a summary table. Explain the code."
  "prose|Write a detailed essay on how operating systems manage virtual memory, page caches and swapping, with examples."
)

if want A; then
  echo "== A: ground-truth traces + simulation"
  for p in "${prompts[@]}"; do
    dom="${p%%|*}"; text="${p#*|}"
    "$MOE_TRACE" -m "$MODEL" -o "$OUT/trace-$dom.csv" -n "$NTOK" --no-repack -p "$text" > /dev/null 2> "$OUT/trace-$dom.log" || { tail -5 "$OUT/trace-$dom.log"; exit 1; }
    tail -1 "$OUT/trace-$dom.log" | grep -v perf || true
    "$B" import-csv "$OUT/trace-$dom.csv" --map "$OUT/map.json" -o "$OUT/trace-$dom.mpt" --note "$dom: $text"
    "$B" analyze "$OUT/trace-$dom.mpt" --map "$OUT/map.json" --json "$OUT/analyze-$dom.json" > "$OUT/analyze-$dom.txt"
    "$B" sim "$OUT/trace-$dom.mpt" --map "$OUT/map.json" --caps 0.15,0.25,0.35,0.5,0.7 --warmup 16 \
      --md "$OUT/sim-$dom.md" --csv "$OUT/sim-$dom.csv" > /dev/null
    head -4 "$OUT/analyze-$dom.txt"
  done
fi

if want C; then
  echo "== C: fault vs bulk read bandwidth on the model file"
  unit=$(python3 -c "import json;m=json.load(open('$OUT/map.json'));u=m['units'];print(sum(x['bytes'] for x in u)//len(u))")
  for s in 1 2 3; do
    for c in 128 0; do
      echo "-- seed $s willneed-chunk-kb $c"; "$B" fault-io --file "$MODEL" --unit-bytes "$unit" --units 32 --threads "$(nproc --ignore=4)" --seed "$s" --willneed-chunk-kb "$c"
    done
  done | tee "$OUT/faultio.txt"
fi

if want B; then
  echo "== B: replay ground truth under a memory wall (simulator validation + mechanism test)"
  for cap in $BUDGETS; do
    for var in ${BVARS:-plain random daemon randaemon}; do
      run="$OUT/B-$cap/$var"; mkdir -p "$run"; cold
      rp=("$B" replay "$MODEL" "$OUT/trace-prose.mpt" --mode mmap --threads 4 --json "$run/replay.json")
      case "$var" in
        plain)  "$root/bench/cgroup_run.sh" "$cap" 0 "$run" -- "${rp[@]}" > "$run/replay.txt" ;;
        random) "$root/bench/cgroup_run.sh" "$cap" 0 "$run" -- "${rp[@]}" --no-readahead > "$run/replay.txt" ;;
        daemon) # completion readahead from moepagerd, inside the same cgroup
          "$root/bench/cgroup_run.sh" "$cap" 0 "$run" -- bash -c '
            "$1" "$2" --ops linux --source sentinel --interval-us 300 --policy v --budget-mb 0 \
              --stats-json "$3/daemon.json" 2> "$3/daemon.log" & pid=$!
            sleep 1; shift 3; "$@"; rc=$?; kill -INT $pid; wait $pid; exit $rc' _ \
            "$D" "$MODEL" "$run" "${rp[@]}" > "$run/replay.txt" ;;
        randaemon) # read-around off (as llama.cpp --lazy-mode on does) + completion readahead
          "$root/bench/cgroup_run.sh" "$cap" 0 "$run" -- bash -c '
            "$1" "$2" --ops linux --source sentinel --interval-us 300 --policy v --budget-mb 0 \
              --stats-json "$3/daemon.json" 2> "$3/daemon.log" & pid=$!
            sleep 1; shift 3; "$@"; rc=$?; kill -INT $pid; wait $pid; exit $rc' _ \
            "$D" "$MODEL" "$run" "${rp[@]}" --no-readahead > "$run/replay.txt" ;;
      esac
      echo "$cap $var: $(cat "$run/replay.txt")"
    done
  done
fi

if want D; then
  echo "== D: end-to-end llama-bench decode under a memory wall"
  : "${LLAMA_BIN:?set LLAMA_BIN for step D}"
  for cap in $BUDGETS; do
    for cfg in ${DCFGS:-B0 B2 D2 L LD}; do
      run="$OUT/D-$cap/$cfg"; mkdir -p "$run"
      lb=(timeout "${RUN_TIMEOUT:-900}" "$LLAMA_BIN/llama-bench" -m "$MODEL" -p 0 -n "$TG" -r 1 -t "$(nproc --ignore=4)" -lm mmap)
      swap=0; daemon=()
      case "$cfg" in
        B0) lb+=(--repack 1); swap=max ;;                       # default: repack into anon memory
        B2) lb+=(--repack 0) ;;                                 # page cache, kernel policy only
        B5) lb+=(--repack 0); daemon=(--policy willneed-all --no-completion) ;;
        D2) lb+=(--repack 0); daemon=(--policy v --budget-mb 0) ;; # completion readahead (pinning needs CAP_IPC_LOCK)
        L)  lb+=(-lzm on) ;;                                    # llama.cpp lazy mode: mmap + MADV_RANDOM, no repack
        LD) lb+=(-lzm on); daemon=(--policy v --budget-mb 0) ;;  # lazy mode + moepagerd completion readahead
      esac
      cold
      "$root/bench/snapshot.sh" "$run/before" "$(lsblk -no PKNAME "$(df --output=source "$MODEL" | tail -1)" | head -1)"
      if [ ${#daemon[@]} -gt 0 ]; then
        "$root/bench/cgroup_run.sh" "$cap" "$swap" "$run" -- bash -c '
          d=$1; m=$2; out=$3; n=$4; shift 4
          "$d" "$m" --ops linux --source sentinel --interval-us 300 --stats-json "$out/daemon.json" "${@:1:$n}" 2> "$out/daemon.log" & pid=$!
          sleep 1; shift "$n"; "$@"; rc=$?; kill -INT $pid; wait $pid; exit $rc' _ \
          "$D" "$MODEL" "$run" "${#daemon[@]}" "${daemon[@]}" "${lb[@]}" > "$run/llama.out" 2> "$run/llama.err" || echo "$cfg failed at $cap"
      else
        "$root/bench/cgroup_run.sh" "$cap" "$swap" "$run" -- "${lb[@]}" > "$run/llama.out" 2> "$run/llama.err" || echo "$cfg failed at $cap"
      fi
      "$root/bench/snapshot.sh" "$run/after" "$(cat "$run/before/device")"
      echo "$cap $cfg: $(grep -E 'tg[0-9]+' "$run/llama.out" | awk -F'|' '{print $(NF-1)}')"
    done
  done
  python3 "$root/python/moepager_tools/collect.py" "$OUT" | tee "$OUT/report.md" || true
fi
echo "results in $OUT"
