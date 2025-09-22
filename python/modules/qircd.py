import sys
import re
import inspect
from dataclasses import dataclass, field
from enum import Enum, IntFlag
from functools import wraps
from datetime import datetime
from uuid import UUID, uuid4
from abc import ABC, abstractmethod
from typing import Dict, List, Optional, Callable, Set

class QIRCModuleType(Enum):
    MODULE =      1 << 0
    BOT =         1 << 1

class QIRCEvent(IntFlag):
    AUTH_SASL_PLAIN     = 1 << 0
    CHANNEL_MSG         = 1 << 1
    PRIVATE_MSG         = 1 << 2
    JOIN                = 1 << 3
    LEAVE               = 1 << 4

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
            def some_handler(self, channel: Channel, acc: Account, msg: Message) -> Message:
                msg.text = msg.text.upper()
                return msg

            @qirc.on(QIRCEvent.CHANNEL_MSG)
            def another_handler(self, channel: Channel, acc: Account, msg: Message) -> Message:
                msg.text = msg.text[::-1]
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
                    "event": event,
                })
        return handlers

@dataclass
class Account:
    name: str
    nick: str
    password: Optional[str] = None
    host: Optional[str] = None

    uid: bytes = field(default_factory=lambda: uuid4())
    creation_date: datetime = field(default_factory=datetime.utcnow)

    channels: List[bytes] = field(default_factory=list)

    connections_count: int = 0

    def prefix(self) -> str:
        """nick!username@host."""
        user = self.name or ""
        host = self.host or "localhost"
        return f"{self.nick}!{user}@{host}"

@dataclass
class Message:
    id: bytes = field(default_factory=lambda: uuid4())

    # IRCv3 message tags (key/value strings, e.g. `time`, `msgid`, `account`, `+draft/reply`)
    tags: Dict[str, Optional[str]] = field(default_factory=dict)

    # prefix information (usually the sender)
    nick: Optional[str] = None
    user: Optional[str] = None
    host: Optional[str] = None

    # target(s) of the PRIVMSG (channels or nicks)
    targets: List[str] = field(default_factory=list)

    account: Optional[Account] = None

    text: bytes = ""
    raw: Optional[bytes] = None

    # message originates from server (NOTICE, CTCP replies, etc.)
    from_server: bool = False

@dataclass
class AuthUserResult:
    result: bool = False
    reason: bytes = None

@dataclass
class Channel:
    """Represents an IRC channel with topic, members, and modes."""
    uid: bytes = field(default_factory=lambda: uuid4())
    name: str = ""
    topic: str = ""
    key: Optional[str] = None
    account_owner_id: Optional[str] = None
    date_creation: datetime = field(default_factory=datetime.utcnow)

    # members: List["Account"] = field(default_factory=list)
    members: List[bytes] = field(default_factory=list)

    # channel modes (like +m, +k, +l etc.)
    modes: Dict[str, Optional[str]] = field(default_factory=dict)

    # ban masks
    ban_masks: Set[str] = field(default_factory=set)

    # limit (like +l)
    limit: Optional[int] = None

@dataclass
class User:
    user_id: str
    username: str
    password: str
    is_authenticated: bool = False

class QIRC:
    _handlers = {}
    _modules = {}

    def call(self, event: QIRCEvent, *args, **kwargs):
        handlers = self._handlers.get(event)
        if not handlers:
            print(f"Error: No handler for event {event}", file=sys.stderr)
            return None

        if event is QIRCEvent.CHANNEL_MSG:
            chan_data, account_data, text = args
            chan = Channel(**chan_data)
            account = Account(**account_data)
            message = Message(id=b"\x00", text=text)
            args = (chan, account, message)

        result = None
        for instance, func in handlers:
            if instance is None:
                continue  # skip unbound handlers
            bound_handler = func.__get__(instance)
            result = bound_handler(*args, **kwargs)
        return result

    @classmethod
    def on(cls, event: QIRCEvent):
        """Decorator to register an event handler."""
        def decorator(func):
            @wraps(func)
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)

            # mark wrapper for introspection
            wrapper._qirc_event = event
            wrapper._qirc_handler = True
            wrapper._qirc_original = func

            cls._handlers.setdefault(event, []).append((None, wrapper))
            return wrapper
        return decorator

    def register_module(self, module: "QIRCModule"):
        self._modules[module.__class__.__name__] = module

        for event, handlers in self._handlers.items():
            new_handlers = []
            for instance, func in handlers:
                if func.__qualname__.startswith(module.__class__.__name__ + "."):
                    instance = module
                new_handlers.append((instance, func))
            self._handlers[event] = new_handlers

    @classmethod
    def list_modules(cls) -> dict:
        """Return descriptions of all registered modules."""
        return {name: mod.describe() for name, mod in cls._modules.items()}

    @classmethod
    def enable_module(cls, name: str):
        """Enable a registered module by name."""
        if name not in cls._modules:
            raise KeyError(f"Module '{name}' is not registered.")
        cls._modules[name].enable()

    @classmethod
    def disable_module(cls, name: str):
        """Disable a registered module by name."""
        if name not in cls._modules:
            raise KeyError(f"Module '{name}' is not registered.")
        cls._modules[name].disable()

    @classmethod
    def interpreter_idx(cls) -> int:
        """Returns the interpreter idx"""
        import __main__
        return getattr(__main__, "INTERPRETER_IDX", -1)

qirc = QIRC()
__qirc_call = lambda *args, **kwargs: qirc.call(*args, **kwargs)
