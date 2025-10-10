# Modules

cIRCa has the Python interpreter embedded. It hooks into the server, and at 
various stages of execution it emits events to the Python runtime where you 
can act on them.

## How-to

To create a module, define a Python class that inherits from `QIRCModule`.  
Your class MUST implement `__init__` with a `super()`, then 
register event callbacks using the `@qirc.on()` decorator.

Your Python module filename must start with `mod_`, and goes into `data/modules/`

## Type completion

While working on your module, we strongly suggest to copy the folder `src/python/qircd/` 
inside your development environment, so that you have type completion for the various 
dataclasses, and events.

### Example

An example module `data/modules/mod_test.py`

```python3
from qircd import *

class MyModule(QIRCModule):
    def __init__(self):
        super().__init__()

    @qirc.on(QIRCEvent.CHANNEL_MSG)
    def channel_message_handler(self, msg: Message) -> Message:
        if msg.account.name == "sander":
            # messages from `sander` are always uppercase now
            msg.text = msg.text.upper()
    
        # all channel messages get this tag attached
        msg.tags["example-tag"] = "example-value"
        return msg

my_mod = MyModule()
qirc.register_module(my_mod)
```

Then reload the server, and enable the module.

you can also implement the `init` and `deinit` methods if you want to do 
something when the module is activated, or deactivated.

# Events

### Raw Message (incoming)

This runs early - before cIRCa has started parsing the IRC line.

```python3
@qirc.on(QIRCEvent.RAW_MSG)
def raw_message_handler(self, msg: RawMessage) -> RawMessage:
    print("raw", msg.raw)
    print("ip", msg.ip)
    return msg
```

### Authentication handler

Implement custom authentication by providing a SASL handler, for example to validate against a database, LDAP, or any external system.

```python3
@qirc.on(QIRCEvent.AUTH_SASL_PLAIN)
def sasl_verify_password(self, auth: AuthUser) -> AuthUser:
    result = auth.username == "sander" and \
             auth.password == "sander"

    if not result:
        auth.cancel("authentication failed")
    return auth
```

### Channel Join

```python3
@qirc.on(QIRCEvent.CHANNEL_JOIN)
def join_handler(self, join: ChannelJoin) -> ChannelJoin:
    print("joining", join.channel.name)

    if join.account.name == "user1":
        join.cancel("not allowed!")

    return join
```

### Channel Message

```python3
@qirc.on(QIRCEvent.CHANNEL_MSG)
def allcaps_handler(self, msg: Message) -> Message:
    # modify all messages to all caps for channel #loud
    if msg.channel.name == "loud":
        msg.text = msg.text.upper()
    return msg
```

# Concurrency

cIRCa runs `x` independent Python interpreters in a thread pool, each with its own Global Interpreter Lock (GIL), as described in [PEP 684](https://peps.python.org/pep-0684/).

Because each interpreter is isolated, your module is loaded separately
into every instance. cIRCa automatically selects which interpreter to use for each call (currently via round-robin).

# Performance

The overhead of invoking a Python callback is low—typically around **250-500 microseconds** for a full C++ → Python → C++ round trip.
