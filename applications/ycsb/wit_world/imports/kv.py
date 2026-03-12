from typing import TypeVar, Generic, Union, Optional, Protocol, Tuple, List, Any, Self
from types import TracebackType
from enum import Flag, Enum, auto
from dataclasses import dataclass
from abc import abstractmethod
import weakref

from ..types import Result, Ok, Err, Some



def get(key: str) -> Optional[bytes]:
    raise NotImplementedError

def set(key: str, value: bytes) -> None:
    raise NotImplementedError

def delete(key: str) -> bool:
    raise NotImplementedError

