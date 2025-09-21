import sys
import re
import inspect
from dataclasses import dataclass, field
from enum import Enum
from functools import wraps
from datetime import datetime
from uuid import UUID, uuid4
from abc import ABC, abstractmethod
from typing import Dict, List, Optional, Callable, Set

class QIRCModuleType(Enum):
    MODULE = 0
    BOT = 1

class QIRCEvent(Enum):
    AUTH_USER = 0
    CHANNEL_MSG = 1
    PRIVATE_MSG = 2
    JOIN = 3
    LEAVE = 4

class QIRCModule:
    """
    Baseclass for modules.

    Usage:
        class TestModule(QIRCModule):
            name = "TestModule"
            version = 0.1
            author = "Sander"
            type = QIRCModuleType.MODULE

            def __init__(self):
                super().__init__()

            def init(self):
                print("module init code here")

            def deinit(self):
                print("module deinit code here")

            @qirc.on(QIRCEvent.CHANNEL_MSG)
            def some_handler(self, acc: Account, msg: Message) -> Message:
                msg.text = msg.text.upper()
                return msg

    Attributes:
        name (str): The name of the module.
        version (float): Module version number.
        author (str): Author of the module.
        type (QIRCModuleType): The type/category of the module.

    Methods:
        init(): Called when the module is enabled.
        deinit(): Called when the module is disabled.
    """
    name: str = None
    version: float = None
    author: str = None
    type: QIRCModuleType = None

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)

        # enforce required metadata
        missing_meta = []
        for attr in ["name", "version", "author", "type"]:
            if getattr(cls, attr, None) is None:
                missing_meta.append(attr)
        if missing_meta:
            raise NotImplementedError(
                f"Subclass {cls.__name__} must define metadata: {', '.join(missing_meta)}"
            )

        # enforce lifecycle methods
        for method in ["init", "deinit"]:
            if not callable(getattr(cls, method, None)):
                raise NotImplementedError(
                    f"Subclass {cls.__name__} must implement: {method}()"
                )

    def __init__(self):
        self._enabled: bool = False

    # lifecycle control
    def enable(self):
        if not self._enabled:
            self._enabled = True
            self.init()

    def disable(self):
        if self._enabled:
            self._enabled = False
            self.deinit()

    @property
    def enabled(self) -> bool:
        return self._enabled

    # introspection
    def describe(self) -> dict:
        return {
            "name": self.name,
            "version": self.version,
            "author": self.author,
            "type": self.type.name if isinstance(self.type, Enum) else self.type,
            "enabled": self.enabled,
            "handlers": self._get_event_handlers(),
        }

    def _get_event_handlers(self):
        """find methods decorated as qirc event handlers."""
        handlers = []
        for name, func in inspect.getmembers(self, predicate=inspect.ismethod):
            event = getattr(func, "_qirc_event", None)
            if event is not None:
                handlers.append({
                    "method": name,
                    "event": str(event),
                })
        return handlers

@dataclass
class Account:
    username: str
    nick: str
    password: Optional[str] = None
    host: Optional[str] = None

    id: str = field(default_factory=lambda: str(uuid4()))
    creation_date: datetime = field(default_factory=datetime.utcnow)

    def prefix(self) -> str:
        """nick!username@host."""
        user = self.username or ""
        host = self.host or "localhost"
        return f"{self.nick}!{user}@{host}"

@dataclass
class Message:
    id: str = field(default_factory=lambda: str(uuid4()))

    # IRCv3 message tags (key/value strings, e.g. `time`, `msgid`, `account`, `+draft/reply`)
    tags: Dict[str, Optional[str]] = field(default_factory=dict)

    # prefix information (usually the sender)
    nick: Optional[str] = None
    user: Optional[str] = None
    host: Optional[str] = None

    # target(s) of the PRIVMSG (channels or nicks)
    targets: List[str] = field(default_factory=list)

    account: Optional[Account] = None

    text: str = ""
    raw: Optional[str] = None

    # message originates from server (NOTICE, CTCP replies, etc.)
    from_server: bool = False

@dataclass
class Channel:
    """Represents an IRC channel with topic, members, and modes."""
    uid: str = field(default_factory=lambda: str(uuid4()))
    name: str = ""
    topic: str = ""
    key: Optional[str] = None
    account_owner_id: Optional[str] = None
    creation_date: datetime = field(default_factory=datetime.utcnow)

    # membership (references to Account objects)
    members: List["Account"] = field(default_factory=list)

    # channel modes (like +m, +k, +l etc.)
    modes: Dict[str, Optional[str]] = field(default_factory=dict)

    # ban masks
    bans: Set[str] = field(default_factory=set)

    # limit (like +l)
    limit: Optional[int] = None

@dataclass
class AuthUserResult:
    result: bool
    reason: str  = None

@dataclass
class User:
    user_id: str
    username: str
    password: str
    is_authenticated: bool = False

class QIRC:
    _handlers = {}

    def call(self, event: QIRCEvent, *args, **kwargs):
        if event not in self._handlers or not self._handlers[event]:
            print(f"Error: No handler for event {event}", file=sys.stderr)
            return None

        result = args[0] if args else None  # typically a Message
        for handler in self._handlers[event]:
            # allow handler to modify/replace result
            result = handler(result, *args[1:], **kwargs)
        return result

    @classmethod
    def on(cls, event: QIRCEvent):
        def decorator(func):
            cls._handlers.setdefault(event, []).append(func)

            @wraps(func)
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)

            # mark wrapper with event info for introspection
            wrapper._qirc_event = event
            wrapper._qirc_handler = True
            wrapper._qirc_original = func

            return wrapper
        return decorator

def is_auth(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        user = args[0] if args else None
        if user and getattr(user, 'is_authenticated', False):
            return func(*args, **kwargs)
        else:
            raise PermissionError("User is not authenticated")
    return wrapper

qirc = QIRC()
__qirc_call = lambda *args, **kwargs: qirc.call(*args, **kwargs)
