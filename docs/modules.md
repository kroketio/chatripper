## Modules

To create a module, define a Python class that inherits from `QIRCModule`.  
Your class MUST implement `__init__` with a `super()`, then 
register event callbacks using the `@qirc.on()` decorator.

#### Example

```python3
class MyModule(QIRCModule):
    def __init__(self):
        super().__init__()

    @qirc.on(QIRCEvent.CHANNEL_MSG)
    def channel_message_handler(self, channel: Channel, acc: Account, msg: Message) -> Message:
        if acc.username == "sander":
            # messages from `sander` are always uppercase now
            msg.text = msg.text.upper()
    
        # all channel messages get this tag attached
        msg.tags["example-tag"] = "example-value"
        return msg
```

Then register the module via:

```python3
my_mod = MyModule()
qirc.register_module(my_mod)
```

## Concurrency

SnakePit runs `x` independent Python interpreters in a thread pool, each with its own Global Interpreter Lock (GIL), as described in [PEP 684](https://peps.python.org/pep-0684/).

Because each interpreter is isolated, your module is loaded separately 
into every instance. SnakePit automatically selects which interpreter to use for each call (currently via round-robin).

### Preserving state

Some modules need to **maintain state** between calls. For example, a module implementing a “slowmode” feature must track when each participant last sent a message to enforce a cooldown period.

Because each Python interpreter in SnakePit is **isolated**, your module’s state exists separately in every interpreter. To maintain consistent state, you can **pin the module to a single interpreter**, ensuring all stateful operations happen in the same context.

#### Cooldown module example

See: `QIRCModuleMode.EXCLUSIVE`

```python3
import time
from typing import Optional

class SlowMode(QIRCModule):
    mode = QIRCModuleMode.EXCLUSIVE  # <== here

    def __init__(self):
        super().__init__()

        # cooldown storage, username:time
        self._last_msg_time: dict[str, float] = {}
        self.cooldown = 5.0

    @qirc.on(QIRCEvent.CHANNEL_MSG)
    def slowmode_handler(self, channel: Channel, acc: Account, msg: Message) -> Optional[Message]:
        username = acc.name if acc.name else acc.nick
        now = time.time()
        last_time = self._last_msg_time.get(username, 0.0)
    
        remaining = self.cooldown - (now - last_time)
        if remaining > 0:
            acc.send_message(f"On cooldown. You can talk again in {remaining:.1f} seconds.")
            return None
    
        self._last_msg_time[username] = now
        return msg
```

## Performance

The overhead of invoking a Python callback is low—typically around **250 microseconds** for a full C++ → Python → C++ round trip.

## Examples

#### External Login Handler

You can implement custom authentication by providing your own SASL handler, for example to validate against a database, LDAP, or any external system.

```python3
@qirc.on(QIRCEvent.AUTH_SASL_PLAIN)
def my_custom_sasl_handler(self, username: str, password: str, ip: str) -> AuthUserResult:
    result = username == "sander" and password == "sander"
    return AuthUserResult(
        result=result,
        reason="bad credentials" if not result else None
    )
```

## Events

todo: describe the various events