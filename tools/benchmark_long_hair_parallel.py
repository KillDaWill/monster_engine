#!/usr/bin/env python3
"""Run the C harness and summarize samples with nearest-rank percentiles."""
import csv
import io
import statistics
import subprocess
import sys


def main():
    binary = sys.argv[1] if len(sys.argv) > 1 else "./benchmarks/benchmark_long_hair_parallel"
    result = subprocess.run([binary, "3", "5"], check=True, text=True, capture_output=True)
    rows = list(csv.DictReader(io.StringIO(result.stdout)))
    print(result.stdout, end="")
    for threads in (1, 2, 4, 8):
        values = sorted(float(row["elapsed_ms"]) for row in rows if int(row["threads"]) == threads)
        if not values:
            raise RuntimeError(f"missing samples for {threads} threads")
        p95 = values[max(0, (95 * len(values) + 99) // 100 - 1)]
        print(f"summary threads={threads} n={len(values)} p50={statistics.median(values):.3f} "
              f"p95={p95:.3f} max={max(values):.3f} ms")


if __name__ == "__main__":
    main()
