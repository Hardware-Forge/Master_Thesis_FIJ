#!/usr/bin/env python3
"""
Heavier version of your filter program to amplify propagation of small faults.
- Repeats the chosen filter many times (default 50) to increase chance a tiny error
  affects the final image.
- Can process in float32 accumulation mode (delays uint8 clipping until the end).
- Emits timing JSON for each run so you can set injector bounds.
- Keeps backward-compatible filter names from your original script.
- NEW: supports key=value CLI style (e.g., input=..., output=..., filter=..., iters=..., float_accum=1)
  so it works with wrappers that only split argv on spaces.

Usage examples:
  python3 filter_heavy.py input=in.png output=out.png filter=gaussian iters=200 float_accum=1 kernel=11
  python3 filter_heavy.py input=in.png output=out.png filter=sobel iters=1000 profile=1
  python3 filter_heavy.py input=in.png output=out.png filter=gaussian iters=500 disk_roundtrip=1 workfile=/tmp/wrk.png
"""
import argparse
import time
import json
import os
import sys
from pathlib import Path

import cv2
import numpy as np


def _truthy(s):
    return str(s).strip().lower() in {"1", "true", "yes", "y", "on"}


def normalize_argv_for_kv_style(argv):
    """
    Convert tokens like:
        ['input=in.png', 'output=out.png', 'filter=gaussian', 'iters=200', 'float_accum=1']
    into argparse-friendly tokens:
        ['in.png', 'out.png', '--filter', 'gaussian', '--iters', '200', '--float-accum']

    Behavior:
    - Tokens starting with '-' are passed through unchanged (standard argparse still works).
    - Bare tokens without '=' are treated as positionals (input/output if placed first).
    - key=value tokens are mapped to the defined argparse flags.
    - Boolean flags accept 1/true/yes/on (case-insensitive) to enable; falsey values omit the flag.

    This lets you use key=value style while keeping original dashed flags compatible.
    """
    # convenient aliases
    alias = {
        "f": "filter",
        "in": "input",
        "out": "output",
        "work": "workfile",
        "work_file": "workfile",
        "work-file": "workfile",
        "float-accum": "float_accum",
        "disk-roundtrip": "disk_roundtrip",
    }

    bool_keys = {"float_accum", "profile", "disk_roundtrip"}
    out_tokens = []

    for tok in argv:
        if tok.startswith("-"):
            # pass through standard flags/args as-is
            out_tokens.append(tok)
            continue

        if "=" not in tok:
            # bare positional (e.g., input or output)
            out_tokens.append(tok)
            continue

        k, v = tok.split("=", 1)
        k_norm = alias.get(k.strip().lower().replace("-", "_"), k.strip().lower().replace("-", "_"))
        flag = f"--{k_norm.replace('_', '-')}"

        if k_norm in {"input", "output"}:
            # treat as positional in the right order
            out_tokens.append(v)
        elif k_norm in bool_keys:
            if _truthy(v):
                out_tokens.append(flag)
            # if falsey, omit entirely
        else:
            out_tokens.extend([flag, v])

    return out_tokens


def ensure_u8(img):
    if img.dtype != np.uint8:
        img = np.clip(img, 0, 255).astype(np.uint8)
    return img


def apply_filter_single(img, name, kernel=5):
    # keep original behavior but allow kernel sizing for some filters
    name = name.lower()
    if name == "identity":
        return img
    if name == "gray":
        return cv2.cvtColor(img, cv2.COLOR_BGR2GRAY)
    if name == "blur":
        return cv2.blur(img, (kernel, kernel))
    if name == "gaussian":
        # if kernel is even, OpenCV will accept (0,0) and sigma; we use kernel if >0
        k = kernel if kernel % 2 == 1 else kernel + 1
        return cv2.GaussianBlur(img, (k, k), 1.2)
    if name == "median":
        k = kernel if kernel % 2 == 1 else kernel + 1
        return cv2.medianBlur(img, k)
    if name == "bilateral":
        # bilateral has fixed d param; kernel used to scale sigma
        return cv2.bilateralFilter(img, d=kernel, sigmaColor=75, sigmaSpace=75)
    if name == "sobel":
        gx = cv2.Sobel(img, cv2.CV_32F, 1, 0, ksize=3)
        gy = cv2.Sobel(img, cv2.CV_32F, 0, 1, ksize=3)
        absx = cv2.convertScaleAbs(gx)
        absy = cv2.convertScaleAbs(gy)
        return cv2.addWeighted(absx, 0.5, absy, 0.5, 0)
    if name == "laplacian":
        lap = cv2.Laplacian(img, cv2.CV_32F, ksize=3)
        return cv2.convertScaleAbs(lap)
    if name == "sharpen":
        k = np.array([[0,-1,0],[-1,5,-1],[0,-1,0]], dtype=np.float32)
        return cv2.filter2D(img, -1, k)
    if name == "unsharp":
        blur = cv2.GaussianBlur(img, (0,0), 2.0)
        return cv2.addWeighted(img, 1.5, blur, -0.5, 0)
    if name == "noise":
        noise = np.random.normal(0, 5, img.shape).astype(np.float32)
        out = img.astype(np.float32) + noise
        return ensure_u8(out)
    raise ValueError(f"Unknown filter: {name}")


def _fsync_file(path):
    """Try to fsync the file to help force on-disk persistence (best-effort)."""
    try:
        with open(path, "rb") as f:
            f.flush()
            os.fsync(f.fileno())
    except Exception:
        # don't crash on fsync failure; it's best-effort
        pass


def apply_heavy(img, name, iters=50, kernel=5, float_accum=False,
                disk_roundtrip=False, workfile=None, imread_flags=cv2.IMREAD_UNCHANGED):
    """Apply `name` filter repeatedly `iters` times.

    If float_accum is True, keep working buffer in float32 and delay uint8 conversion until the end.

    If disk_roundtrip is True, after each iteration the function will:
      - write the current buffer to `workfile` (uint8)
      - fsync the file (best-effort)
      - re-read it with cv2.imread(workfile, imread_flags)
    and continue the next iteration from that read image.

    This forces an on-disk roundtrip and makes any external silent corruption (disk injector)
    visible as the output will be used for the next iteration.
    """
    if float_accum:
        buf = img.astype(np.float32)
    else:
        buf = img.copy()

    if disk_roundtrip and not workfile:
        raise ValueError("disk_roundtrip requested but no workfile provided")

    for i in range(iters):
        # apply per-iteration using the single-step function
        # adapt behavior depending on buffer dtype
        if float_accum and buf.dtype == np.float32:
            # apply filter in float by converting to the expected input dtype
            tmp_in = ensure_u8(buf)  # some OpenCV ops expect uint8; use clipped view
            out = apply_filter_single(tmp_in, name, kernel=kernel)
            # convert back to float for accumulation
            buf = out.astype(np.float32)
        else:
            buf = apply_filter_single(buf, name, kernel=kernel)

        # if requested, do on-disk roundtrip: write and re-read (overwrite workfile)
        if disk_roundtrip:
            # write a uint8 view to disk
            to_write = ensure_u8(buf)
            ok = cv2.imwrite(workfile, to_write)
            if not ok:
                raise SystemExit(f"Failed to write workfile during roundtrip: {workfile}")
            # try to fsync file to encourage the write to reach persistent storage
            _fsync_file(workfile)
            # re-read back into memory
            reloaded = cv2.imread(workfile, imread_flags)
            if reloaded is None:
                raise SystemExit(f"Failed to re-read workfile during roundtrip: {workfile}")
            if float_accum:
                buf = reloaded.astype(np.float32)
            else:
                buf = reloaded

        # small optional progress heartbeat for large iters (no heavy IO)
        if (i + 1) % 100 == 0:
            # lightweight flushable print so long batch runs can be monitored by the harness
            print(json.dumps({"progress_iter": i+1}), flush=True)

    if float_accum:
        return ensure_u8(buf)
    return buf


def main():
    p = argparse.ArgumentParser(description="Apply a heavier image filter (repeated passes).")
    p.add_argument("input", help="Path to input image")
    p.add_argument("output", help="Path to save output image")
    p.add_argument("--filter", "-f", default="identity",
                   help="Filter: identity|gray|blur|gaussian|median|bilateral|sobel|laplacian|sharpen|unsharp|noise")
    p.add_argument("--iters", type=int, default=50, help="Number of times to repeat the filter")
    p.add_argument("--kernel", type=int, default=5, help="Kernel size / strength where applicable")
    p.add_argument("--float-accum", action="store_true", help="Keep working buffer in float32 to delay clipping")
    p.add_argument("--seed", type=int, default=None, help="Seed numpy random (useful for deterministic noise)")
    p.add_argument("--profile", action="store_true", help="Print timing JSON line after run")
    # new disk roundtrip options:
    p.add_argument("--disk-roundtrip", action="store_true",
                   help="After each iteration write output to disk and re-read it (forces I/O roundtrip).")
    p.add_argument("--workfile", help="Path used for on-disk roundtrip; defaults to the final output path if not set.")

    # Parse using key=value normalization (while still supporting standard argparse flags)
    norm_argv = normalize_argv_for_kv_style(sys.argv[1:])
    args = p.parse_args(norm_argv)

    if args.seed is not None:
        np.random.seed(args.seed)

    t0 = time.perf_counter()
    img = cv2.imread(args.input, cv2.IMREAD_UNCHANGED)
    if img is None:
        raise SystemExit(f"Failed to read: {args.input}")
    t1 = time.perf_counter()

    # choose workfile: if disk roundtrip is requested and no workfile given, use args.output
    workfile = args.workfile if args.workfile else args.output

    out = apply_heavy(img, args.filter, iters=args.iters, kernel=args.kernel,
                      float_accum=args.float_accum,
                      disk_roundtrip=args.disk_roundtrip, workfile=workfile,
                      imread_flags=cv2.IMREAD_UNCHANGED)

    if out.dtype != np.uint8:
        out = ensure_u8(out)
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    ok = cv2.imwrite(args.output, out)
    if not ok:
        raise SystemExit(f"Failed to write: {args.output}")
    t2 = time.perf_counter()

    if args.profile:
        print(json.dumps({
            "input": str(args.input),
            "filter": args.filter,
            "iters": args.iters,
            "kernel": args.kernel,
            "float_accum": bool(args.float_accum),
            "disk_roundtrip": bool(args.disk_roundtrip),
            "workfile": str(workfile) if workfile else None,
            "read_ms": (t1 - t0) * 1000.0,
            "process_ms": (t2 - t1) * 1000.0,
            "total_ms": (t2 - t0) * 1000.0,
            "pid": os.getpid(),
        }))


if __name__ == "__main__":
    main()
