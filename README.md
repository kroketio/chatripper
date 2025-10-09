# cIRCa

Fast, self-hosted, highly scriptable chat platform for team communications.

**Work in progress**

## Design philosophy

cIRCa is an IRC server, but unlike others. Read [more](docs/design.md).

## Features

* Multitenancy
* Roles/permissions system
* Accounts, history, metadata, previews, video/audio calls
* Built-in bouncer
* No config, configuration happens through the admin webif
* Server-side scripting with Python
* Web chat client
* REST API
* Generate invite links

## Performance

cIRCa is designed to operate primarily in memory. It is both multithreaded and 
asynchronous. It can handle thousands of concurrent connections. I/O operations are 
queued whenever possible, and concurrency is prioritized to deliver a fast, responsive chat 
experience - low-powered hardware included.

## Server-side scripting

Change messages in transit, modify/add IRCv3 message tags, and more.

```python3
from qircd import *

class MyModule(QIRCModule):
    def __init__(self):
        super().__init__()

    @qirc.on(QIRCEvent.CHANNEL_MSG)
    def channel_message_handler(self, msg: Message) -> Message:
        print(f"new message in {msg.channel.name}")

        if msg.account.name == "sander":
            # messages from `sander` are always uppercase now
            msg.text = msg.text.upper()

        # all channel messages get this tag attached
        msg.tags["example-tag"] = "example-value"
        return msg

my_mod = MyModule()
qirc.register_module(my_mod)
```

`Channel` and `Account` objects are available for any additional logic.

More info [docs/modules.md](docs/modules.md)

## Quick start guide

```bash
docker compose build --no-cache
docker compose run --rm circa
```

Visit the web-interface over at [http://127.0.0.1:3000](http://127.0.0.1:3000).

For more information about Docker: [docs/docker.md](docs/docker.md)

### Compiling

[docs/building.md](docs/building.md).

## Testimonials

> &lt;user1&gt; What problem does this solve? 

> &lt;user2&gt; Lua would be a better choice for a scripting engine than Python

> &lt;user3&gt; you can finish the IRC server in Hell with all the other C++ programmers  
> &lt;user3&gt; and Python programmers  
> &lt;user3&gt; us Perl/Go programmers are going to heaven

> &lt;user4&gt; What is your elevator pitch?
 
> &lt;user5&gt; Qt is bloated  
> &lt;user5&gt; what if I want to run this on ARM?

> &lt;user6&gt; to be honest I'm not sure if individual message signatures are of much use really