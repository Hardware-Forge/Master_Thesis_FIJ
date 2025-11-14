from __future__ import annotations

import json
import ctypes
from pathlib import Path
from datetime import datetime, timezone
from typing import Any, Mapping, Optional


def _to_plain(obj: Any) -> Any:
    """
    Convert common Python, ctypes, and container types into JSON-serializable data.
    Tailored for ctypes.Structure like FijResult.
    """
    if obj is None or isinstance(obj, (bool, int, float, str)):
        return obj
    if isinstance(obj, (bytes, bytearray)):
        return bytes(obj).decode(errors="ignore")

    # ctypes handling
    if isinstance(obj, ctypes.Structure):
        # Use declared _fields_ order for stability
        return {name: _to_plain(getattr(obj, name)) for name, *_ in getattr(obj, "_fields_", [])}
    if isinstance(obj, ctypes.Array):
        try:
            return bytes(obj).decode(errors="ignore")
        except Exception:
            return [_to_plain(v) for v in obj]

    # Containers / mappings
    if isinstance(obj, Mapping):
        return {str(k): _to_plain(v) for k, v in obj.items()}
    if isinstance(obj, (list, tuple, set)):
        return [_to_plain(v) for v in obj]

    return str(obj)


def log_injection_iteration(
    base_path: str | Path,
    i: int,
    dt_seconds: float,
    fij_result: ctypes.Structure,
) -> Path:
    """
    Create "<base_path>/injection_{i}/injection_{i}.json" with pretty JSON.

    Parameters
    ----------
    base_path : str | Path
        Root directory for campaign logs (already created by your code).
    i : int
        Loop index from the injection phase.
    dt_seconds : float
        Elapsed time for this iteration in seconds.
    fij_result : ctypes.Structure
        The FijResult returned by the kernel (ctypes.Structure with _fields_).

    context : Optional[Mapping[str, Any]]
        Optional extra metadata (device, label, delays, etc.).

    Returns
    -------
    Path
        Path to the created "injection_{i}" folder.
    """
    base = Path(base_path)
    folder = base / f"injection_{i}"
    folder.mkdir(parents=True, exist_ok=True)

    payload = {
        "iteration": i,
        "timestamp": datetime.now(timezone.utc).isoformat(),
        "duration_ms": dt_seconds * 1000.0,
        "result": _to_plain(fij_result),
    }

    out_file = folder / f"injection_{i}.json"
    with out_file.open("w", encoding="utf-8") as f:
        json.dump(payload, f, indent=2, sort_keys=True, ensure_ascii=False)

    return folder
