import ctypes

from structs import FijExec

# Internal ioctl bitfield constants
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
_IOC_READ  = 2


def _IOC(direction: int, type_: int, nr: int, size: int) -> int:
    return (
        (direction << _IOC_DIRSHIFT)
        | (type_   << _IOC_TYPESHIFT)
        | (nr      << _IOC_NRSHIFT)
        | (size    << _IOC_SIZESHIFT)
    )


def _IOW(type_char: str, nr: int, struct_type: type) -> int:
    size = ctypes.sizeof(struct_type)
    return _IOC(_IOC_WRITE, ord(type_char), nr, size)


def _IOR(type_char: str, nr: int, struct_type: type) -> int:
    size = ctypes.sizeof(struct_type)
    return _IOC(_IOC_READ, ord(type_char), nr, size)


def _IOWR(type_char: str, nr: int, struct_type: type) -> int:
    size = ctypes.sizeof(struct_type)
    return _IOC(_IOC_READ | _IOC_WRITE, ord(type_char), nr, size)


# Public constant used by the rest of the code
IOCTL_EXEC_AND_FAULT = _IOWR('f', 2, FijExec)
