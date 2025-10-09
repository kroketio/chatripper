import sys
import re
import inspect
from dataclasses import dataclass, field
from enum import Enum, IntFlag
from functools import wraps
from datetime import datetime, timezone
from uuid import UUID, uuid4
from abc import ABC, abstractmethod
from typing import Dict, List, Optional, Callable, Set

@dataclass(kw_only=True)
class QEventBase:
    reason: str = None
    _cancel: bool = False

    def cancel(self, message: str):
        self._cancel = True
        if message != "":
            self.reason = message

@dataclass(kw_only=True)
class QClass:
    _dirty: set = field(default_factory=set, init=False, repr=False)
    _initialized: bool = field(default=False, init=False, repr=False)

    def __post_init__(self):
        self._initialized = True

    def __setattr__(self, key, value):
        # skip tracking before initialization and internal attributes
        if key in {"_dirty", "_initialized"} or (key.startswith("_") and key != "_cancel"):
            object.__setattr__(self, key, value)
            return

        # normal dirty tracking, but only after init
        if getattr(self, "_initialized", False):
            old_value = getattr(self, key, None)
            if old_value != value:
                self._dirty.add(key)

        object.__setattr__(self, key, value)

@dataclass
class Channel(QClass):
    uid: bytes = field(default_factory=lambda: uuid4())
    name: str = ""
    topic: str = ""
    key: Optional[str] = None
    owner: Optional[str] = None
    date_creation: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    # members: List["Account"] = field(default_factory=list)
    members: List[bytes] = field(default_factory=list)

    # channel modes (like +m, +k, +l etc.)
    modes: Dict[str, Optional[str]] = field(default_factory=dict)

    # ban masks
    ban_masks: Set[str] = field(default_factory=set)

    # limit (like +l)
    limit: Optional[int] = None


@dataclass
class Account(QClass):
    name: str
    nick: str
    password: Optional[str] = None
    host: Optional[str] = None

    uid: bytes = field(default_factory=lambda: uuid4())
    creation_date: datetime = field(default_factory=lambda: datetime.now(timezone.utc))

    channels: List[bytes] = field(default_factory=list)

    connections_count: int = 0

    def prefix(self) -> str:
        """nick!username@host."""
        user = self.name or ""
        host = self.host or "localhost"
        return f"{self.nick}!{user}@{host}"
