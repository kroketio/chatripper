import sys, re
from dataclasses import dataclass, field
from enum import Enum
from functools import wraps
from uuid import UUID, uuid4
from typing import Callable

class IRCEvent(Enum):
    AUTH_USER = 0
    CHANNEL_MSG = 1
    PRIVATE_MSG = 2
    JOIN = 3
    LEAVE = 4

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
        print(event, type(event))
        if event == IRCEvent.AUTH_USER.value:
            u = User(*args)
        print("event", event)

        # if event in self._handlers:
        #     return self._handlers[event](*args, **kwargs)
        # print(f"Error: No handler for event {event}", file=sys.stderr)

    @classmethod
    def on(cls, event: IRCEvent):
        def decorator(func):
            cls._handlers[event] = func

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
