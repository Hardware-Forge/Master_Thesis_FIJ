#!/usr/bin/env python3
import os
import json
import ctypes
from dataclasses import dataclass
from typing import List, Dict, Any
from structs import FijParams

# ---- FIJ register IDs (must match enum fij_reg_id in fij.h) ----
FIJ_REG_NONE = 0
FIJ_REG_RAX  = 1
FIJ_REG_RBX  = 2
FIJ_REG_RCX  = 3
FIJ_REG_RDX  = 4
FIJ_REG_RSI  = 5
FIJ_REG_RDI  = 6
FIJ_REG_RBP  = 7
FIJ_REG_RSP  = 8
FIJ_REG_RIP  = 9  # PC

_REG_NAME_MAP = {
    "rax": FIJ_REG_RAX,
    "rbx": FIJ_REG_RBX,
    "rcx": FIJ_REG_RCX,
    "rdx": FIJ_REG_RDX,
    "rsi": FIJ_REG_RSI,
    "rdi": FIJ_REG_RDI,
    "rbp": FIJ_REG_RBP,
    "rsp": FIJ_REG_RSP,
    "pc":  FIJ_REG_RIP,
    "rip": FIJ_REG_RIP,
}



def reg_name_to_id(name: str) -> int:
    if not name:
        return FIJ_REG_NONE
    return _REG_NAME_MAP.get(name.lower(), FIJ_REG_NONE)


#
# ---- Helpers for safely filling ctypes char arrays ----
#

import ctypes

def _set_cstring(struct, field: str, value: str):
    """
    Set a c_char[N] field on a ctypes.Structure safely from a Python str.

    struct: the ctypes.Structure instance (e.g. FijParams)
    field:  the field name as a string (e.g. "process_path")
    """
    if value is None:
        value = ""
    data = value.encode("utf-8")

    # Find the field type and capacity from the struct definition
    for name, ctype in struct._fields_:
        if name == field:
            # Expect an array of c_char
            if not (issubclass(ctype, ctypes.Array) and ctype._type_ is ctypes.c_char):
                raise TypeError(f"{field} is not a c_char array field")
            max_len = ctype._length_  # total capacity including NUL
            break
    else:
        raise AttributeError(f"{field} not found in {struct.__class__.__name__}")

    # Truncate to leave room for terminating NUL
    if len(data) >= max_len:
        data = data[: max_len - 1]

    # Direct assignment to the struct field copies into the underlying buffer
    setattr(struct, field, data)



def _get_cstring(c_array) -> str:
    return bytes(c_array).split(b"\0", 1)[0].decode("utf-8", "ignore")


def _set_process_name_from_path(p: FijParams):
    path = _get_cstring(p.process_path)
    if not path:
        return
    base = os.path.basename(path)
    _set_cstring(p, "process_name", base)


#
# ---- Apply default/normalization rules (ported from your C code) ----
#

def fij_params_apply_defaults(p: FijParams):
    # Ensure process_name if missing and path is set
    if _get_cstring(p.process_name) == "" and _get_cstring(p.process_path) != "":
        _set_process_name_from_path(p)

    # Normalize booleans (0/1)
    p.only_mem        = 1 if p.only_mem        else 0
    p.thread_present  = 1 if p.thread_present  else 0
    p.all_threads     = 1 if p.all_threads     else 0
    p.process_present = 1 if p.process_present else 0
    p.no_injection    = 1 if p.no_injection    else 0

    # Clamp weight
    if p.weight_mem < 0:
        p.weight_mem = 0

    # Register targeting
    if p.target_reg == 0:
        p.target_reg = FIJ_REG_NONE

    # Thread default
    if not p.thread_present:
        p.thread = 0

    # reg_bit handling
    if not p.reg_bit_present:
        p.reg_bit = 0
    else:
        if p.reg_bit < 0:
            p.reg_bit = 0
        if p.reg_bit > 63:
            p.reg_bit = 63

    # PC targeting
    if not p.target_pc_present:
        p.target_pc = 0  # ignored by kernel

    # Delay sanity
    if p.min_delay_ms and p.max_delay_ms and p.max_delay_ms < p.min_delay_ms:
        tmp = p.min_delay_ms
        p.min_delay_ms = p.max_delay_ms
        p.max_delay_ms = tmp


#
# ---- Small utilities ----
#

def _bool_int(v) -> int:
    if isinstance(v, bool):
        return 1 if v else 0
    if isinstance(v, (int, float)):
        return 1 if v != 0 else 0
    if isinstance(v, str):
        return 1 if v.strip().lower() in ("1", "true", "yes", "on") else 0
    return 0


def _apply_field_if_present(p: FijParams, cfg: Dict[str, Any],
                            key: str, attr: str, *, boolean=False):
    if key in cfg:
        v = cfg[key]
        if boolean:
            setattr(p, attr, _bool_int(v))
        else:
            setattr(p, attr, int(v))


#
# ---- Public object returned to caller ----
#

@dataclass
class FijJob:
    path: str         # executable path
    args: str         # argument string
    runs: int         # how many times to execute
    params: FijParams # fully prepared parameters for this (path,args)


#
# ---- Core builder: JSON dict -> List[FijJob] ----
#

def build_fij_jobs_from_config(config: Dict[str, Any]) -> List[FijJob]:
    """
    Build a list of FijJob from a parsed JSON config.

    Precedence:
      global defaults < target.defaults < arg-level entries

    For each (target.path, arg.value/args) combination we create ONE FijJob:
      - job.runs is the resolved runs for that combo
      - job.params is a single FijParams with all options applied
    """
    global_defaults = config.get("defaults", {})
    targets = config.get("targets", [])
    base_path = config.get("base_path", "")

    jobs: List[FijJob] = []

    for t in targets:
        raw_path = t.get("path")
        if not raw_path:
            raise ValueError("Each target must have a 'path'")

        # Replace {base_path}
        if base_path:
            path = raw_path.replace("{base_path}", base_path)
        else:
            path = raw_path

        target_defaults = {}
        target_defaults.update(global_defaults)
        target_defaults.update(t.get("defaults", {}))

        args_list = t.get("args", [])
        if not args_list:
            # even without explicit args, we create one job for the path
            args_list = [ {} ]

        for arg_cfg in args_list:
            # effective config for this (path, args)
            merged = {}
            merged.update(global_defaults)
            merged.update(t.get("defaults", {}))
            merged.update(arg_cfg)

            runs = int(merged.get("runs", 1))
            if runs <= 0:
                continue

            # Build a single FijParams for this path+args
            p = FijParams()

            # path
            _set_cstring(p, "process_path", path)
            _set_process_name_from_path(p)

            # args: accept "value" or "args"
            arg_val = merged.get("value", "") or merged.get("args", "")
            # replace {base_path} to the base_path value if specified
            if isinstance(arg_val, str) and base_path:
                arg_val = arg_val.replace("{base_path}", base_path)
            _set_cstring(p, "process_args", arg_val)

            # numeric / boolean fields
            _apply_field_if_present(p, merged, "weight_mem",   "weight_mem")
            _apply_field_if_present(p, merged, "min_delay_ms", "min_delay_ms")
            _apply_field_if_present(p, merged, "max_delay_ms", "max_delay_ms")

            _apply_field_if_present(p, merged, "only_mem",     "only_mem",     boolean=True)
            _apply_field_if_present(p, merged, "no_injection", "no_injection", boolean=True)
            _apply_field_if_present(p, merged, "all_threads",  "all_threads",  boolean=True)

            # thread / process selection
            if "thread" in merged:
                p.thread_present = 1
                p.thread = int(merged["thread"])

            if "nprocess" in merged:
                p.process_present = 1
                p.nprocess = int(merged["nprocess"])

            # optional PC / register / bit (if supplied in JSON)
            if "pc" in merged:
                p.target_pc_present = 1
                pc_val = merged["pc"]
                if isinstance(pc_val, str):
                    p.target_pc = int(pc_val, 0)
                else:
                    p.target_pc = int(pc_val)

            if "reg" in merged:
                p.target_reg = reg_name_to_id(str(merged["reg"]))

            if "bit" in merged:
                p.reg_bit_present = 1
                p.reg_bit = int(merged["bit"])

            # normalize like the C helper
            fij_params_apply_defaults(p)

            job = FijJob(
                path=path,
                args=arg_val if isinstance(arg_val, str) else "",
                runs=runs,
                params=p,
            )
            jobs.append(job)

    return jobs


#
# ---- Convenience: read from JSON file path ----
#

def load_fij_jobs_from_file(config_path: str) -> List[FijJob]:
    """
    Read a JSON config from file (supports // comments) and return List[FijJob].
    """
    with open(config_path, "r", encoding="utf-8") as f:
        text = f.read()

    # strip // comments (simple line-based)
    cleaned_lines = []
    for line in text.splitlines():
        if "//" in line:
            line = line.split("//", 1)[0]
        cleaned_lines.append(line)
    config = json.loads("\n".join(cleaned_lines))

    return build_fij_jobs_from_config(config)


# If you want a quick manual test:
if __name__ == "__main__":
    import sys
    if len(sys.argv) != 2:
        print(f"Usage: {sys.argv[0]} CONFIG.json")
        raise SystemExit(1)

    for j in load_fij_jobs_from_file(sys.argv[1]):
        print(
            f"path={j.path} args='{j.args}' runs={j.runs} "
            f"weight_mem={j.params.weight_mem}"
        )
