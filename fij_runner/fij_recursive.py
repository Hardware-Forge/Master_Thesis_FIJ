#!/usr/bin/env python3
import os
import sys
import time
import argparse
import fcntl
import ctypes
import errno

# ------------------------
# 1. Mirror struct fij_params
# ------------------------

class FijParams(ctypes.Structure):
    _fields_ = [
        ("process_name",      ctypes.c_char * 256),
        ("process_path",      ctypes.c_char * 256),
        ("process_args",      ctypes.c_char * 256),
        ("target_pc",         ctypes.c_int),
        ("target_pc_present", ctypes.c_int),
        ("target_reg",        ctypes.c_int),
        ("reg_bit",           ctypes.c_int),
        ("reg_bit_present",   ctypes.c_int),
        ("weight_mem",        ctypes.c_int),
        ("only_mem",          ctypes.c_int),
        ("min_delay_ms",      ctypes.c_int),
        ("max_delay_ms",      ctypes.c_int),
        ("thread_present",    ctypes.c_int),
        ("thread",            ctypes.c_int),
        ("all_threads",       ctypes.c_int),
        ("nprocess",          ctypes.c_int),
        ("process_present",   ctypes.c_int),
        ("no_injection",      ctypes.c_int),
    ]

# ------------------------
# 2. IOCTL: _IOW('f', 2, struct fij_params)
# ------------------------

_IOC_NRBITS   = 8
_IOC_TYPEBITS = 8
_IOC_SIZEBITS = 14
_IOC_DIRBITS  = 2

_IOC_NRSHIFT   = 0
_IOC_TYPESHIFT = _IOC_NRSHIFT   + _IOC_NRBITS
_IOC_SIZESHIFT = _IOC_TYPESHIFT + _IOC_TYPEBITS
_IOC_DIRSHIFT  = _IOC_SIZESHIFT + _IOC_SIZEBITS

_IOC_NONE  = 0
_IOC_WRITE = 1

def _IOC(direction, type_, nr, size):
    return ((direction << _IOC_DIRSHIFT) |
            (type_     << _IOC_TYPESHIFT) |
            (nr        << _IOC_NRSHIFT)   |
            (size      << _IOC_SIZESHIFT))

def _IOW(type_char, nr, struct_type):
    size = ctypes.sizeof(struct_type)
    return _IOC(_IOC_WRITE, ord(type_char), nr, size)

IOCTL_EXEC_AND_FAULT = _IOW('f', 2, FijParams)

# ------------------------
# Helper: clone FijParams
# ------------------------

def clone_fij_params(src: FijParams) -> FijParams:
    dst = FijParams()
    ctypes.memmove(
        ctypes.addressof(dst),
        ctypes.addressof(src),
        ctypes.sizeof(FijParams),
    )
    return dst

# ------------------------
# 3. Single ioctl run using a FijParams object
# ------------------------

def run_once(device, base_params: FijParams, no_injection, max_delay_ms):
    """
    Perform a single IOCTL_EXEC_AND_FAULT using a FijParams template.

    base_params:
        A FijParams instance with all relevant fields initialized by the caller.
        This function will NOT try to 'fix up' uninitialized values, it only
        overrides:
          - no_injection
          - max_delay_ms (when no_injection == False)
    """
    # Work on a copy so we don't mutate the caller's object
    p = clone_fij_params(base_params)

    p.no_injection = 1 if no_injection else 0
    if not no_injection:
        p.max_delay_ms = int(max_delay_ms)

    start = time.perf_counter()
    fd = os.open(device, os.O_RDWR)
    try:
        fcntl.ioctl(fd, IOCTL_EXEC_AND_FAULT, p)
    finally:
        os.close(fd)
    end = time.perf_counter()

    return end - start

def run_with_retries(device,
                     base_params: FijParams,
                     no_injection,
                     max_delay_ms,
                     pre_delay_ms=50,
                     max_retries=5,
                     retry_delay_ms=50):
    """
    Wrapper around run_once with EBUSY retry handling.
    """
    if pre_delay_ms > 0:
        time.sleep(pre_delay_ms / 1000.0)

    attempt = 0
    while True:
        try:
            return run_once(
                device=device,
                base_params=base_params,
                no_injection=no_injection,
                max_delay_ms=max_delay_ms,
            )
        except OSError as e:
            if e.errno == errno.EBUSY and attempt < max_retries:
                attempt += 1
                time.sleep(retry_delay_ms / 1000.0)
                continue
            raise

# ------------------------
# 4. Reusable campaign function (now takes FijParams)
# ------------------------

def run_injection_campaign(
    device,
    base_params: FijParams,
    runs,
    baseline_runs=5,
    pre_delay_ms=50,
    max_retries=5,
    retry_delay_ms=50,
    verbose=True,
):
    """
    Run a full injection campaign using a caller-provided FijParams object.

    Steps:
      1) Run `baseline_runs` no-injection executions.
      2) Compute max_delay_ms = round(min_baseline_time_ms), clamped to >= 1.
      3) Run `runs` executions with injection using that max_delay_ms.

    The caller is responsible for correctly initializing all relevant fields
    in `base_params` (process_name, process_path, etc.). This function only
    adjusts:
      - no_injection
      - max_delay_ms (for injection phase)

    Parameters:
        device (str): FIJ device node path.
        base_params (FijParams): Fully initialized parameters template.
        runs (int): Number of IOCTL calls WITH injection.
        baseline_runs (int): Number of baseline (no injection) runs.
        pre_delay_ms, max_retries, retry_delay_ms: retry timing controls.
        verbose (bool): If True, prints progress and summary.

    Returns:
        dict as before.
    """
    if not os.path.exists(device):
        raise OSError(errno.ENOENT, f"Device {device} does not exist")

    if runs <= 0:
        raise ValueError("runs must be > 0")
    if baseline_runs <= 0:
        raise ValueError("baseline_runs must be > 0")

    # Try to get a readable label from base_params (safe even if not perfect)
    try:
        raw_path = bytes(base_params.process_path).split(b'\0', 1)[0]
        label_path = raw_path.decode(errors="ignore")
    except Exception:
        label_path = ""
    try:
        raw_args = bytes(base_params.process_args).split(b'\0', 1)[0]
        label_args = raw_args.decode(errors="ignore")
    except Exception:
        label_args = ""

    label = f"{label_path} '{label_args}'" if label_args else label_path or "<unknown>"

    # Optional: if process_path is set, check it exists (non-fatal if empty)
    if label_path and not os.path.exists(label_path):
        raise OSError(errno.ENOENT, f"Target path {label_path} does not exist")

    if verbose:
        print(f"=== Campaign start for: {label}")
        print(f"  runs={runs}")
        print(f"  device={device}")
        print()

    # Phase 1: baseline
    if verbose:
        print(f"Phase 1: running {baseline_runs} baseline IOCTL calls (no_injection=1)")
    baseline_times = []

    for i in range(baseline_runs):
        try:
            dt = run_with_retries(
                device=device,
                base_params=base_params,
                no_injection=True,
                max_delay_ms=0,  # ignored in no_injection mode
                pre_delay_ms=pre_delay_ms,
                max_retries=max_retries,
                retry_delay_ms=retry_delay_ms,
            )
            baseline_times.append(dt)
            if verbose:
                print(f"  Baseline run {i+1}/{baseline_runs}: {dt*1000:.3f} ms")
        except OSError as e:
            if verbose:
                print(f"  Baseline run {i+1} failed: {e}", file=sys.stderr)

    if not baseline_times:
        raise RuntimeError(
            f"All baseline runs failed for target {label}; cannot determine max_delay_ms."
        )

    min_time_s = min(baseline_times)
    max_delay_ms = int(round(min_time_s * 1000.0))
    if max_delay_ms <= 0:
        max_delay_ms = 1

    if verbose:
        print("\nBaseline summary:")
        print(f"  Successful baseline runs: {len(baseline_times)}/{baseline_runs}")
        print(f"  Minimum baseline time: {min_time_s*1000:.3f} ms")
        print(f"  Selected max_delay_ms: {max_delay_ms} ms")

    # Phase 2: injection
    if verbose:
        print(f"\nPhase 2: running {runs} IOCTL calls with injection "
              f"(no_injection=0, max_delay_ms={max_delay_ms})")

    inj_times = []
    for i in range(runs):
        try:
            dt = run_with_retries(
                device=device,
                base_params=base_params,
                no_injection=False,
                max_delay_ms=max_delay_ms,
                pre_delay_ms=pre_delay_ms,
                max_retries=max_retries,
                retry_delay_ms=retry_delay_ms,
            )
            inj_times.append(dt)
            if verbose:
                print(f"  Injection run {i+1}/{runs}: {dt*1000:.3f} ms")
        except OSError as e:
            if verbose:
                print(f"  Injection run {i+1} failed: {e}", file=sys.stderr)

    if not inj_times:
        raise RuntimeError(
            f"All injection runs failed for target {label}."
        )

    avg = sum(inj_times) / len(inj_times)
    if len(inj_times) > 1:
        var = sum((t - avg) ** 2 for t in inj_times) / (len(inj_times) - 1)
    else:
        var = 0.0
    std = var ** 0.5

    if verbose:
        print("\nInjection summary:")
        print(f"  Successful runs: {len(inj_times)}/{runs}")
        print(f"  Average: {avg*1000:.3f} ms")
        print(f"  Std dev: {std*1000:.3f} ms")
        print("=== Campaign end ===\n")

    return {
        "baseline_runs": baseline_runs,
        "baseline_success": len(baseline_times),
        "baseline_min_ms": min_time_s * 1000.0,
        "max_delay_ms": max_delay_ms,
        "injection_requested": runs,
        "injection_success": len(inj_times),
        "avg_ms": avg * 1000.0,
        "std_ms": std * 1000.0,
        "inj_times_ms": [t * 1000.0 for t in inj_times],
    }

# ------------------------
# 5. CLI helper: build params if caller doesn't provide one
# ------------------------

def build_fij_params_from_args(path: str, args_str: str, weight_mem: int) -> FijParams:
    p = FijParams()

    # Basic, safe initialization for CLI usage
    pname = os.path.basename(path).encode()[:255]
    ppath = path.encode()[:255]
    pargs = args_str.encode()[:255] if args_str else b""

    p.process_name = pname
    p.process_path = ppath
    p.process_args = pargs

    p.weight_mem = int(weight_mem)

    # Any additional fields you want defaulted at CLI level can go here.
    # Everything else can be set by external code when not using CLI.

    return p

# ------------------------
# 6. CLI using the campaign function
# ------------------------

def main():
    ap = argparse.ArgumentParser(
        description=(
            "Run IOCTL_EXEC_AND_FAULT with automatic delay selection.\n"
            "1) Run baseline executions with no injection.\n"
            "2) Set max_delay_ms = round(min_baseline_time_ms).\n"
            "3) Run requested number of injection executions using that delay."
        )
    )
    ap.add_argument("--path", required=True, help="Path to target executable")
    ap.add_argument("--args", default="", help="Arguments string for the target")
    ap.add_argument(
        "--runs",
        type=int,
        default=10,
        help="Number of IOCTL runs WITH injection after the baseline runs (default: 10)"
    )
    ap.add_argument(
        "--device",
        default="/dev/fij",
        help="FIJ device node (default: /dev/fij)"
    )
    ap.add_argument(
        "--weight-mem",
        type=int,
        default=0,
        help="Set weight_mem in fij_params (default: 0)"
    )
    ap.add_argument(
        "--baseline-runs",
        type=int,
        default=5,
        help="Number of baseline runs (default: 5)"
    )

    args = ap.parse_args()

    # For the CLI we construct a FijParams, but library users can pass their own.
    params = build_fij_params_from_args(
        path=args.path,
        args_str=args.args,
        weight_mem=args.weight_mem,
    )

    try:
        run_injection_campaign(
            device=args.device,
            base_params=params,
            runs=args.runs,
            baseline_runs=args.baseline_runs,
            pre_delay_ms=50,
            max_retries=5,
            retry_delay_ms=50,
            verbose=True,
        )
    except (OSError, RuntimeError, ValueError) as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)

if __name__ == "__main__":
    main()
