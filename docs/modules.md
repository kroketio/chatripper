## Modules

To create a module, define a Python class that inherits from `QIRCModule`.  
Your class MUST implement `__init__`, `init()`, and `deinit()`, then 
register event callbacks using the `@qirc.on()` decorator.

#### Example

```python3
class MyModule(QIRCModule):
    name = "MyModule"
    version = 0.1
    author = "Sander"
    type = QIRCModuleType.MODULE

    def __init__(self):
        super().__init__()

    def init(self):
        print("any init code")

    def deinit(self):
        print("any deinit code")

    @qirc.on(QIRCEvent.CHANNEL_MSG)
    def channel_message_handler(self, channel: Channel, acc: Account, msg: Message) -> Message:
        if acc.username == "sander":
            # messages from `sander` are always uppercase now
            msg.text = msg.text.upper()
    
        # all channel messages get this tag attached
        msg.tags["example-tag"] = "example-value"
        return msg
```

## Concurrency

SnakePit runs `x` independent Python interpreters in a thread pool, each with its own Global Interpreter Lock (GIL), as described in [PEP 684](https://peps.python.org/pep-0684/).

Because each interpreter is isolated, your module is loaded separately into every instance. SnakePit automatically selects which interpreter to use for each call (currently via round-robin scheduling).

This design allows the server to be both multithreaded and asynchronous: a single blocking callback won’t stall others, and multiple callbacks can execute concurrently without interference.

The overhead of invoking a Python callback is low—typically around **250 microseconds** for a full C++ → Python → C++ round trip.

## Events

todo: describe the various events

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