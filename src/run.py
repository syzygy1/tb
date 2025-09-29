#!/usr/bin/env python3
"""
Python port of run.pl

Replicates the logic of the original Perl script for generating/verifying Syzygy-like
retrograde tablebases using helper binaries (rtbgen/rtbgenp, rtbver/rtbverp).

- Builds TB IDs like "KQvK", "KQvKR", "KQNvKBP", etc.
- Filters by total piece count (kings included) with --min/--max.
- Optional generation (--generate) and verification (--verify).
- Optional Huffman verification (--huffman) when a .rtbw file exists.
- Optional disk hint (--disk) that passes -d only when len==max (matching run.pl).
- Thread count via --threads (passed to tools with -t).
- Respects RTBWDIR, defaulting to "." (like the Perl version).
"""
import argparse
import os
import subprocess
from itertools import combinations_with_replacement
from pathlib import Path

def build_parser():
    p = argparse.ArgumentParser(description="Generate/verify RTB tablesets (Python port of run.pl)")
    p.add_argument("--threads", type=int, default=1, help="Number of threads (default: 1)")
    p.add_argument("--generate", action="store_true", help="Run generators (rtbgen/rtbgenp)")
    p.add_argument("--verify", action="store_true", help="Run verifiers (rtbver/rtbverp) with --log")
    p.add_argument("--huffman", action="store_true", help="If .rtbw exists, run verifier with -h")
    p.add_argument("--disk", action="store_true", help="Pass -d (only when piece-count == --max)")
    p.add_argument("--min", dest="min_pcs", type=int, default=3, help="Minimum total pieces including both kings (default: 3)")
    p.add_argument("--max", dest="max_pcs", type=int, default=4, help="Maximum total pieces including both kings (default: 4)")
    return p

PIECES = ["Q","R","B","N","P"]

def multiset_sequences(k):
    """Non-decreasing sequences (multiset combinations) of length k from PIECES."""
    if k == 0:
        yield []
        return
    for combo in combinations_with_replacement(PIECES, k):
        yield list(combo)

def run_cmd(cmd):
    print(f"$ {cmd}")
    # Use shell=True to mirror Perl system "string" behavior.
    subprocess.run(cmd, shell=True, check=False)

def process_tb(tb, args):
    # piece count is len(tb) - 1 (the Perl script used this formula)
    total_pieces = len(tb) - 1
    if total_pieces < args.min_pcs or total_pieces > args.max_pcs:
        return

    dopt = "-d " if args.disk and total_pieces == args.max_pcs else ""

    # Non-pawn vs pawn-aware tool choice
    has_pawn = ("P" in tb)

    # Generate if requested and .rtbz does not exist yet
    if args.generate and not Path(f"{tb}.rtbz").exists():
        print(f"Generating {tb}")
        if not has_pawn:
            run_cmd(f"rtbgen {dopt}-t {args.threads} --stats {tb}")
        else:
            run_cmd(f"rtbgenp {dopt}-t {args.threads} --stats {tb}")

    # Verify if requested
    if args.verify:
        print(f"Verifying {tb}")
        if not has_pawn:
            run_cmd(f"rtbver -t {args.threads} --log {tb}")
        else:
            run_cmd(f"rtbverp -t {args.threads} --log {tb}")

    # Huffman verify if requested and .rtbw exists
    if args.huffman and Path(f"{tb}.rtbw").exists():
        if not has_pawn:
            run_cmd(f"rtbver -h {tb}")
        else:
            run_cmd(f"rtbverp -h {tb}")

def main():
    # Default RTBWDIR="." as in Perl
    os.environ.setdefault("RTBWDIR", ".")

    args = build_parser().parse_args()

    # We generate *all* classes up to 5-man by iterating over attacker/defender piece counts.
    # This generalizes the explicit nested loops in the Perl script while preserving set ordering.
    # Total pieces = 2 (kings) + a_count + d_count
    for a_count in range(1, 4):  # attacker has 1..3 pieces
        for d_count in range(0, 3):  # defender has 0..2 pieces
            total = 2 + a_count + d_count
            # We'll rely on process_tb() to filter by --min/--max,
            # but we can skip obviously-too-large sets early.
            if total > args.max_pcs:
                continue

            for a_seq in multiset_sequences(a_count):
                for d_seq in multiset_sequences(d_count):
                    tb = "K" + "".join(a_seq) + "v" + "K" + "".join(d_seq)
                    process_tb(tb, args)

if __name__ == "__main__":
    main()
