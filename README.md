## qircd

Qt6 C++ IRCv3 server for a modern chat experience.

Work in progress.

- account based (SASL login)
  - allow multiple clients per account
  - always-on functionality
- scrollbacks/history
- realm based ('create servers')
- allow anonymous logins
- web invites
- admin web-interface, API
- SQL backend (sqlite or Postgres)
- Embedded [Meilisearch](https://github.com/meilisearch/meilisearch/) for full-text searches
- Embedded Python interpreter(s) for server-side modules
- implement various IRCv3 extensions
- Keep backward compatibility with older IRC clients
- audio/video calls
- Easy to install
- Easy to configure
- Runs on a potato

## Server-side scripting

This server runs `x` number of Python interpreters in a threadpool, each with its 
own GIL (see [PEP 684](https://peps.python.org/pep-0684/)), and hooks into the 
IRC server.

#### Channel messages

We can change messages on-the-fly, and modify/add IRCv3 message tags in transit.

```python3
@qirc.on(QIRCEvent.CHANNEL_MSG)
def channel_message_handler(self, channel: Channel, acc: Account, msg: Message) -> Message:
  if acc.username == "sander":
    # messages from `sander` are always uppercase now
    msg.text = msg.text.upper()

  # all channel messages get this tag attached
  msg.tags["example-tag"] = "example-value"
  return msg
```

`Channel` and `Account` objects are available for any additional logic.

This example call (C++ -> Python -> C++) took 280 microseconds.

#### External login handler

We may contact our own database, LDAP, or whatever verifies credentials by 
supplying our own SASL authentication handler.

```python3
@qirc.on(QIRCEvent.AUTH_SASL_PLAIN)
def my_custom_sasl_handler(self, username: str, password: str, ip: str) -> AuthUserResult:
  result = username == "sander" and password == "sander"
  return AuthUserResult(result=result, reason="bad credentials" if not result else None)
```

### Modules

The above 2 examples are part of the qircd module system, and are just Python 
files you place in a directory, and are loaded at runtime.

```python3
class TestModule(QIRCModule):
    name = "TestModule"
    version = 0.1
    author = "Sander"
    type = QIRCModuleType.MODULE

    def __init__(self):
        super().__init__()

    def init(self):
        print("any init code")

    def deinit(self):
        print("any deinit code")

    # [some decorated methods here that subscribe to QIRCEvent(s)]
```

### Requirements

- Linux
- Qt6
- Python >= 3.13
- CMake
- C++17