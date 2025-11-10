import ctypes


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


class FijResult(ctypes.Structure):
    _fields_ = [
        ("status", ctypes.c_int),
        ("target_tgid", ctypes.c_int),
        ("fault_injected", ctypes.c_int),
        ("duration_ns", ctypes.c_ulonglong),
        ("seq_no", ctypes.c_ulonglong),
    ]


class FijExec(ctypes.Structure):
    _fields_ = [
        ("params", FijParams),
        ("result", FijResult),
    ]


def clone_fij_params(src: FijParams) -> FijParams:
    """
    Return a deep binary copy of FijParams.

    Keeps callers from accidentally mutating shared state.
    """
    dst = FijParams()
    ctypes.memmove(
        ctypes.addressof(dst),
        ctypes.addressof(src),
        ctypes.sizeof(FijParams),
    )
    return dst
