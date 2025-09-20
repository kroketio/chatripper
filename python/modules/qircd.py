import sys, re
from dataclasses import dataclass, field
from enum import Enum
from functools import wraps
from datetime import datetime
from uuid import UUID, uuid4
from typing import Dict, List, Optional, Callable, Set

class IRCEvent(Enum):
    AUTH_USER = 0
    CHANNEL_MSG = 1
    PRIVATE_MSG = 2
    JOIN = 3
    LEAVE = 4

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

    def call(self, event: IRCEvent, *args, **kwargs):
        if event not in self._handlers or not self._handlers[event]:
            print(f"Error: No handler for event {event}", file=sys.stderr)
            return None

        result = args[0] if args else None  # typically a Message
        for handler in self._handlers[event]:
            # allow handler to modify/replace result
            result = handler(result, *args[1:], **kwargs)
        return result

    @classmethod
    def on(cls, event: IRCEvent):
        def decorator(func):
            cls._handlers.setdefault(event, []).append(func)

            @wraps(func)
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)
            return wrapper
        return decorator

def is_auth(func):
    @wraps(func)
    def wrapper(*args, **kwargs):
        # Here we assume first argument is `user` with `is_authenticated` attribute
        user = args[0] if args else None
        if user and getattr(user, 'is_authenticated', False):
            return func(*args, **kwargs)
        else:
            raise PermissionError("User is not authenticated")
    return wrapper

qirc = QIRC()
__qirc_call = lambda *args, **kwargs: qirc.call(*args, **kwargs)
