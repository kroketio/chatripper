# cIRCa

cIRCA is a modern approach to IRC, reimagining the protocol, server architecture, and client features
from the ground up. Our focus is **solving real-world challenges**, while remaining compatible
with existing clients.

**Work in progress**

## Design philosophy

* A strong focus on utility, practicality, and serviceability
* Embrace modern chat features like accounts, history, metadata, previews, video/audio calls
* High customizability via an admin web-interface (no messing around in config files)
* Server-side scripting with Python
* Integrated web chat client
* REST API

## Performance

cIRCa is designed to operate primarily in memory. It is both multithreaded and 
asynchronous, and while we haven’t tested it, we are confident it can handle thousands 
of concurrent connections.

We regularly benchmark key parts of the code to ensure optimal performance. I/O operations are 
queued whenever possible, and concurrency is prioritized to deliver a fast, responsive chat 
experience - low-powered hardware included.

## Server-side scripting

Change messages in transit, modify/add IRCv3 message tags, and more.

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