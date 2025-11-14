from pathlib import Path
from typing import Union
import os

PathLike = Union[str, os.PathLike]

def create_dir_in_path(base_path: PathLike, final_folder: str) -> Path:
    """
    Ensure a directory exists at ``base_path / final_folder``.

    Behavior:
    - If ``base_path`` doesn't exist it is created.
    - If ``final_folder`` doesn't exist, it's created and returned.
    - If ``final_folder`` already exists, try ``final_folder(1)``, ``final_folder(2)``, ...
      until a new directory can be created. This preserves previous logs.

    Returns the absolute :class:`pathlib.Path` for convenience.
    """
    base = Path(base_path)
    # Ensure base directory exists first (create parent folders)
    base.mkdir(parents=True, exist_ok=True)

    candidate = final_folder
    i = 1
    while True:
        target = base / candidate
        try:
            # Attempt to create directory atomically; fail if it already exists
            target.mkdir(exist_ok=False)
            return target.resolve()
        except FileExistsError:
            # Already exists (could be dir or file). Try a new candidate name.
            candidate = f"{final_folder}({i})"
            i += 1
