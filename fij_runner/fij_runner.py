import errno
import fcntl
import os
import sys
import time
from typing import Dict, List, Tuple
from pathlib import Path
import re

from fij_logger import log_injection_iteration
from dir_utils import create_dir_in_path
from structs import FijParams, FijResult, FijExec, clone_fij_params
from ioctl_codes import IOCTL_EXEC_AND_FAULT


def run_once(
    device: str,
    base_params: FijParams,
    no_injection: bool,
    max_delay_ms: int,
) -> Tuple[float, FijResult]:
    """
    Perform a single IOCTL_EXEC_AND_FAULT on `device` using cloned base_params.

    Returns:
        (duration_seconds, FijResult)
    """
    msg = FijExec()
    msg.params = clone_fij_params(base_params)

    msg.params.no_injection = 1 if no_injection else 0
    if not no_injection:
        msg.params.max_delay_ms = int(max_delay_ms)

    start = time.perf_counter()
    fd = os.open(device, os.O_RDWR)
    try:
        fcntl.ioctl(fd, IOCTL_EXEC_AND_FAULT, msg)
    finally:
        os.close(fd)
    end = time.perf_counter()

    return end - start, msg.result


def run_with_retries(
    device: str,
    base_params: FijParams,
    no_injection: bool,
    max_delay_ms: int,
    pre_delay_ms: int = 50,
    max_retries: int = 5,
    retry_delay_ms: int = 50,
) -> Tuple[float, FijResult]:
    """
    Wrap run_once with:
      - optional pre-delay,
      - retry on EBUSY up to max_retries.
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


def _label_from_params(params: FijParams) -> str:
    """
    logs & errors.
    """
    def _extract(raw: bytes) -> str:
        return bytes(raw).split(b"\0", 1)[0].decode(errors="ignore")

    try:
        path = _extract(params.process_path)
    except Exception:
        path = ""
    try:
        args = _extract(params.process_args)
    except Exception:
        args = ""

    if path and args:
        return f"{path} '{args}'"
    if path:
        return path
    return "<unknown>"


def run_injection_campaign(
    device: str,
    base_params: FijParams,
    runs: int,
    baseline_runs: int = 5,
    pre_delay_ms: int = 50,
    max_retries: int = 5,
    retry_delay_ms: int = 50,
    verbose: bool = True,
) -> Dict[str, object]:
    """
    Run a full injection campaign using a caller-provided FijParams object.

    Steps:
      1) Run `baseline_runs` executions with no injection.
      2) Compute max_delay_ms = round(min_baseline_time_ms), clamped to >= 1.
      3) Run `runs` executions with injection using that max_delay_ms.

    The caller is responsible for setting all relevant fields in base_params.
    This function only modifies:
      - no_injection
      - max_delay_ms (during injection phase)
    """
    if not os.path.exists(device):
        raise OSError(errno.ENOENT, f"Device {device} does not exist")

    if runs <= 0:
        raise ValueError("runs must be > 0")
    if baseline_runs <= 0:
        raise ValueError("baseline_runs must be > 0")

    label = _label_from_params(base_params)

    # Optional existence check for target binary
    # Only if process_path is set.
    raw_path = bytes(base_params.process_path).split(b'\0', 1)[0].decode(errors="ignore")
    if raw_path and not os.path.exists(raw_path):
        raise OSError(errno.ENOENT, f"Target path {raw_path} does not exist")

    if verbose:
        print(f"=== Campaign start for: {label}")
        print(f"  runs={runs}")
        print(f"  device={device}")
        print()


    # Create dir to contain the logs of the injection campaign
    def _cstr(buf: bytes) -> str:
        return bytes(buf).split(b"\0", 1)[0].decode(errors="ignore")

    # simple slug so the folder name is filesystem-friendly
    _slug_re = re.compile(r"[^A-Za-z0-9._-]+")
    def _slug(s: str) -> str:
        return _slug_re.sub("_", s.strip()).strip("_").lower()

    # --- build "<filename>+<args>" ---
    filename   = Path(_cstr(base_params.process_path)).name     #take last part to get the filename
    args_str   = _cstr(base_params.process_args)

    parts = [_slug(filename)]
    if args_str:
        parts += ["+", _slug(args_str)]

    logs_folder = "_".join(parts)  # e.g., myprog.bin_+_v_1.2

    path = create_dir_in_path("../fij_logs", logs_folder)

    args_template = _cstr(base_params.process_args)

    new_path = path / "no_inj"

    baseline_run_dir = new_path / "injection_0"
    #create directory that can contain program output when not injecting faults
    baseline_run_dir.mkdir(parents=True, exist_ok=True)


    # Phase 1: baseline (no injection)
    if verbose:
        print(f"Phase 1: running {baseline_runs} baseline IOCTL calls (no_injection=1)")

    baseline_times: List[float] = []
    baseline_results: List[FijResult] = []

    for i in range(baseline_runs):
        try:
            # here the args are modified so that the process outputs in a no_inj folder for that
            # path+args campaing
            
            expanded_args = (
                args_template
                .replace("{campaign}", str(new_path))
                .replace("{run}", "0")
            )

            base_params.process_args = expanded_args.encode() + b"\0"

            dt, res = run_with_retries(
                device=device,
                base_params=base_params,
                no_injection=True,
                max_delay_ms=0,
                pre_delay_ms=pre_delay_ms,
                max_retries=max_retries,
                retry_delay_ms=retry_delay_ms,
            )
            baseline_times.append(dt)
            baseline_results.append(res)
            if verbose:
                print(f"  Baseline run {i+1}/{baseline_runs}: {dt * 1000.0:.3f} ms")
        except OSError as e:
            if verbose:
                print(f"  Baseline run {i+1} failed: {e}", file=sys.stderr)

    if not baseline_times:
        raise RuntimeError(
            f"All baseline runs failed for target {label}; cannot determine max_delay_ms."
        )

    min_time_s = min(baseline_times)
    max_delay_ms = int(round(min_time_s * 1000.0)) or 1

    if verbose:
        print("\nBaseline summary:")
        print(f"  Successful baseline runs: {len(baseline_times)}/{baseline_runs}")
        print(f"  Minimum baseline time: {min_time_s * 1000.0:.3f} ms")
        print(f"  Selected max_delay_ms: {max_delay_ms} ms")

    # Phase 2: injection
    if verbose:
        print(
            f"\nPhase 2: running {runs} IOCTL calls with injection "
            f"(no_injection=0, max_delay_ms={max_delay_ms})"
        )

    inj_times: List[float] = []
    inj_results: List[FijResult] = []

    for i in range(runs):
        try:
            run_ith_directory = path / f"injection_{i}"
            run_ith_directory.mkdir(parents=True, exist_ok=True)
            # here the args are modified so that the output path of the process becomes the one of 
            # the ith fault injection
            expanded_args = (
                args_template
                .replace("{campaign}", str(path))
                .replace("{run}", str(i))
            )

            base_params.process_args = expanded_args.encode() + b"\0"

            dt, res = run_with_retries(
                device=device,
                base_params=base_params,
                no_injection=False,
                max_delay_ms=max_delay_ms,
                pre_delay_ms=pre_delay_ms,
                max_retries=max_retries,
                retry_delay_ms=retry_delay_ms,
            )
            print(f"dt={dt:.6f}s, target={res.target_tgid}, duration={res.duration_ns}, ec={res.exit_code}")
            inj_times.append(dt)
            inj_results.append(res)
            log_injection_iteration(
                base_path=path,   # this is the campaign folder from create_dir_in_path
                i=i,              # loop index
                dt_seconds=dt,
                fij_result=res,   # raw FijResult from the kernel
            )
            if verbose:
                print(f"  Injection run {i+1}/{runs}: {dt * 1000.0:.3f} ms")
        except OSError as e:
            if verbose:
                print(f"  Injection run {i+1} failed: {e}", file=sys.stderr)

    if not inj_times:
        raise RuntimeError(f"All injection runs failed for target {label}.")

    avg = sum(inj_times) / len(inj_times)
    if len(inj_times) > 1:
        var = sum((t - avg) ** 2 for t in inj_times) / (len(inj_times) - 1)
    else:
        var = 0.0
    std = var ** 0.5

    if verbose:
        print("\nInjection summary:")
        print(f"  Successful runs: {len(inj_times)}/{runs}")
        print(f"  Average: {avg * 1000.0:.3f} ms")
        print(f"  Std dev: {std * 1000.0:.3f} ms")
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
