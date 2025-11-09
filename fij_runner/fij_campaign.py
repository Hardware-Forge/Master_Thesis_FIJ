from __future__ import annotations

import argparse

from json_reader import load_fij_jobs_from_file
from fij_recursive import run_injection_campaign


def run_campaigns_from_config(
    config_path: str,
    device: str = "/dev/fij",
    baseline_runs: int = 5,
    pre_delay_ms: int = 50,
    max_retries: int = 5,
    retry_delay_ms: int = 50,
    verbose: bool = True,
) -> None:
    """
    Load FijJobs from a JSON config file and run an injection campaign for each.

    Assumes:
      - json_reader.load_fij_jobs_from_file(config_path) -> List[FijJob]
      - Each FijJob has: path, args, runs, params (params is a ready-to-use FijParams)
      - run_injection_campaign(device, base_params, runs, ...)
    """
    jobs = load_fij_jobs_from_file(config_path)

    if verbose:
        print(f"[+] Loaded {len(jobs)} jobs from {config_path}")

    for idx, job in enumerate(jobs, start=1):
        if verbose:
            print(
                f"\n[+] Running job {idx}/{len(jobs)}:"
                f"\n    path   = {job.path}"
                f"\n    args   = {job.args}"
                f"\n    runs   = {job.runs}"
            )

        run_injection_campaign(
            device=device,
            base_params=job.params,
            runs=job.runs,
            baseline_runs=baseline_runs,
            pre_delay_ms=pre_delay_ms,
            max_retries=max_retries,
            retry_delay_ms=retry_delay_ms,
            verbose=verbose,
        )


def main() -> None:
    parser = argparse.ArgumentParser(
        description="Run FIJ injection campaigns from a JSON config file."
    )
    # Single required positional: the JSON config path
    parser.add_argument(
        "config_path",
        help="Path to the FIJ jobs configuration JSON file.",
    )

    args = parser.parse_args()
    run_campaigns_from_config(config_path=args.config_path)


if __name__ == "__main__":
    main()
