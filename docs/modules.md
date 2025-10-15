# Modules

chatripper has the Python interpreter embedded. It hooks into the server, and at 
various stages of execution it emits events to the Python runtime where you 
can act on them.

## How-to

To create a module, define a Python class that inherits from `QIRCModule`.  
Your class MUST implement `__init__` with a `super()`, then 
register event callbacks using the `@qirc.on()` decorator.

Your Python module filename MUST start with `mod_`, and goes into `data/modules/`

## Type completion

While working on your module, we suggest to copy the folder `src/python/qircd/` 
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

#### Models

For the dataclass models, see: 

- [src/python/qircd/events.py](../src/python/qircd/events.py) 
- [src/python/qircd/models.py](../src/python/qircd/models.py)

#### Cancel

All events have the `.cancel(message: str)` method, in case you want to cancel them.

### Raw Message (incoming)

This runs early - before chatripper has started parsing the IRC line.

```python3
@qirc.on(QIRCEvent.RAW_MSG)
def raw_message_handler(self, msg: RawMessage) -> RawMessage:
    print("raw", msg.raw)
    print("ip", msg.ip)
    return msg
```

### Private Message

Incoming private message.

```python3
@qirc.on(QIRCEvent.PRIVATE_MSG)
def private_message_handler(self, msg: Message) -> Message:
    print("from", msg.account)
    print("to", msg.dest)
    return msg
```

### Channel Message

Incoming channel message.

```python3
@qirc.on(QIRCEvent.CHANNEL_MSG)
def allcaps_handler(self, msg: Message) -> Message:
    # modify all messages to all caps for channel #loud
    if msg.channel.name == "loud":
        msg.text = msg.text.upper()
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

User is about to join a channel.

```python3
@qirc.on(QIRCEvent.CHANNEL_JOIN)
def join_handler(self, ev: ChannelJoin) -> ChannelJoin:
    print("joining", ev.channel.name)

    if ev.account.name == "user1":
        ev.cancel("not allowed!")

    return ev
```

### Channel Part

User is about to leave a channel.

```python3
@qirc.on(QIRCEvent.CHANNEL_PART)
def channel_leave_handler(self, ev: ChannelPart) -> ChannelPart:
    print("channel part", ev.account.name_or_nick())
    ev.message = "Face the wrath of a thousand suns."
    return ev
```

### Channel rename

```python3
@qirc.on(QIRCEvent.CHANNEL_RENAME)
def channel_rename(self, event: ChannelRename) -> ChannelRename:
    print(event.old_name, event.new_name)
    return event
```

### Nick change

Intercept an IRC nick change.

```python3
@qirc.on(QIRCEvent.NICK_CHANGE)
def nick_change(self, ev: NickChange) -> NickChange:
    print(ev.old_nick, ev.new_nick)

    # enforce random nicknames
    ev.new_nick = random.choice(["foo", "bar", "bob", "alice"])
    return ev
```

### Peer Max Connections

Emitted when an IP has reached its maximum allowed connections to the IRC 
server. Use this event in case you want to add this IP to some external firewall.

```python3
@qirc.on(QIRCEvent.PEER_MAX_CONNECTIONS)
def max_conns_handler(self, ev: PeerMaxConnections) -> PeerMaxConnections:
    print(ev.ip)
    print("number of connections:", ev.connections)
    return ev
```

# Concurrency

chatripper runs `x` independent Python interpreters in a thread pool, each with its own Global Interpreter Lock (GIL), as described in [PEP 684](https://peps.python.org/pep-0684/).

Because each interpreter is isolated, your module is loaded separately
into every instance. chatripper automatically selects which interpreter to use for each call (currently via round-robin).

# Performance

The overhead of invoking a Python callback is low—typically around **250-500 microseconds** for a full C++ → Python → C++ round trip.

# How it works

On the C++ side, Qt classes (and also those annotated with `Q_GADGET` as to avoid 
having to construct a full `Q_OBJECT`) are converted to Python dataclasses, at runtime, using 
introspection, and vice versa.

On the Python side, we track for dataclass member mutations, and mark them as dirty if 
modified by the user, as to not update unnecessarily when we modify the C++ object after 
leaving Python.

It (probably) works similar to [thp/pyotherside](https://github.com/thp/pyotherside) except that we 
have a focus on Python's dataclasses.
