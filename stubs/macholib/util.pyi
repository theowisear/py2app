""" Minimal stubs """
import typing

NOT_SYSTEM_FILES: typing.List[str]

def in_system_path(filename: str) -> bool: ...
def is_platform_file(filename: str) -> bool: ...
def copy2(src: str, dst: str) -> None: ...
def mergecopy(src: str, dest: str) -> None: ...
def mergetree(
    src: str,
    dst: str,
    condition: typing.Callable[[str], bool] | None = None,
    copyfn: typing.Callable[[str, str], None] = mergecopy,
    srcbase: str | None = None,
) -> None: ...
def move(src: str, dst: str) -> None: ...