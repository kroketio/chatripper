# ========================================================================
# PRIVATE MODULE
# ========================================================================
# This file is considered an internal/private part of the QIRCd framework.
# It is typically not intended for direct use in modules.
#
# Direct interaction with this file should generally only occur when
# writing or testing QIRCd modules or extending the framework itself.
# ========================================================================
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

from .models import *
from .events import *

_INTERPRETER_IDX = -1
_IS_DEBUG = False

try:
    from __main__ import snake
    _IS_DEBUG = snake.is_debug()
    _INTERPRETER_IDX = snake.interpreter_idx()
except ImportError as ex:
    print(ex)

def QPrint(msg, file=sys.stdout):
    if not _IS_DEBUG:
        return
    timestamp = datetime.now().strftime("%H:%M:%S")
    prefix = f"[{timestamp}][{_INTERPRETER_IDX}]"
    print(prefix, msg, file=file)

class QIRCModuleType(Enum):
    MODULE =      1 << 0
    BOT =         1 << 1

class QIRCModuleMode(Enum):
    CONCURRENT =      1 << 0
    EXCLUSIVE =       1 << 1

class QIRCEvent(IntFlag):
    AUTH_SASL_PLAIN     = 1 << 0
    CHANNEL_MSG         = 1 << 1
    PRIVATE_MSG         = 1 << 2
    CHANNEL_JOIN        = 1 << 3
    CHANNEL_LEAVE       = 1 << 4

class QIRCModule:
    """
    Baseclass for modules.

    Ensures that the necessary methods exist, as
    well as providing some helper functions for
    introspection
    """
    version: float = None
    author: str = None
    type: QIRCModuleType = QIRCModuleType.MODULE
    mode: QIRCModuleMode = QIRCModuleMode.CONCURRENT

    def __init_subclass__(cls, **kwargs):
        super().__init_subclass__(**kwargs)

        # enforce required metadata
        missing_meta = []
        for attr in ["type", "mode"]:
            if getattr(cls, attr, None) is None:
                missing_meta.append(attr)
        if missing_meta:
            raise NotImplementedError(
                f"Subclass {cls.__name__} must define metadata: {', '.join(missing_meta)}"
            )

    def __init__(self):
        self._enabled: bool = False

    @property
    def enabled(self) -> bool:
        return self._enabled

    @enabled.setter
    def enabled(self, value: bool):
        if self._enabled == value:
            return

        self._enabled = value
        if value:
            init = getattr(self, "init", None)
            if callable(init):
                init()
        else:
            deinit = getattr(self, "deinit", None)
            if callable(deinit):
                deinit()

    # introspection
    def describe(self) -> dict:
        return {
            "name": self.__class__.__name__,
            "version": self.version,
            "author": self.author,
            "type": self.type.value,
            "mode": self.mode.value,
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

class QIRC:
    _handlers = {}
    _modules: dict[str, QIRCModule] = {}

    def __init__(self):
        _interpreter_idx: int = -1
        _debug: bool = False

    def call(self, event: "QIRCEvent", *args, **kwargs):
        handlers = self._handlers.get(event)
        if not handlers:
            if _IS_DEBUG:
                QPrint(f"Error: No handler for event {event}", file=sys.stderr)
            return None

        result = None
        for instance, func in handlers:
            if instance is None:
                continue  # skip unbound handlers

            # skip if module is disabled
            if hasattr(instance, "enabled") and not instance.enabled:
                if _IS_DEBUG:
                    QPrint(f"skipping disabled module {instance.__class__.__name__}")
                continue

            bound_handler = func.__get__(instance)
            result = bound_handler(*args, **kwargs)

            if result is None:
                if _IS_DEBUG:
                    QPrint("returning None")
                return result

        return result

    @classmethod
    def on(cls, event: "QIRCEvent", timeout: float = 1.5):
        """Decorator to register an event handler with optional timeout."""
        def decorator(func):
            @wraps(func)
            def wrapper(*args, **kwargs):
                return func(*args, **kwargs)

            # attach metadata for later
            wrapper._qirc_event = event
            wrapper._qirc_timeout = timeout
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
    def list_modules(cls) -> dict[str, str]:
        return {name: mod.describe() for name, mod in cls._modules.items()}

    @classmethod
    def enable_module(cls, name: str):
        if name not in cls._modules:
            raise KeyError(f"Module '{name}' is not registered.")
        cls._modules[name].enabled = True

    @classmethod
    def disable_module(cls, name: str):
        if name not in cls._modules:
            raise KeyError(f"Module '{name}' is not registered.")
        cls._modules[name].enabled = False

    @classmethod
    def interpreter_idx(cls) -> int:
        return _INTERPRETER_IDX

    @classmethod
    def is_debug(cls) -> bool:
        return _IS_DEBUG

qirc = QIRC()
__qirc_call = lambda *args, **kwargs: qirc.call(*args, **kwargs)
