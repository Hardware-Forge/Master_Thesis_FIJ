import argparse
import os
import sys

from .structs import FijParams
from .runner import run_injection_campaign


def build_fij_params_from_args(
    path: str,
    args_str: str,
    weight_mem: int,
) -> FijParams:
    """
    Construct a minimal FijParams instance from CLI inputs.

    Library users are free to build FijParams themselves instead.
    """
    p = FijParams()

    pname = os.path.basename(path).encode()[:255]
    ppath = path.encode()[:255]
    pargs = args_str.encode()[:255] if args_str else b""

    p.process_name = pname
    p.process_path = ppath
    p.process_args = pargs

    p.weight_mem = int(weight_mem)

    return p


def main() -> None:
    parser = argparse.ArgumentParser(
        description=(
            "Run IOCTL_EXEC_AND_FAULT with automatic delay selection.\n"
            "1) Run baseline executions with no injection.\n"
            "2) Set max_delay_ms = round(min_baseline_time_ms).\n"
            "3) Run requested number of injection executions using that delay."
        )
    )
    parser.add_argument("--path", required=True, help="Path to target executable")
    parser.add_argument("--args", default="", help="Arguments string for the target")
    parser.add_argument(
        "--runs",
        type=int,
        default=10,
        help="Number of IOCTL runs WITH injection after the baseline runs (default: 10)",
    )
    parser.add_argument(
        "--device",
        default="/dev/fij",
        help="FIJ device node (default: /dev/fij)",
    )
    parser.add_argument(
        "--weight-mem",
        type=int,
        default=0,
        help="Set weight_mem in fij_params (default: 0)",
    )
    parser.add_argument(
        "--baseline-runs",
        type=int,
        default=5,
        help="Number of baseline runs (default: 5)",
    )

    args = parser.parse_args()

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
