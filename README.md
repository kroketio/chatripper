# chatripper

Fast, self-hosted, highly scriptable chat platform for team communications.

**Work in progress**

### Goals

chatripper is based on IRC. Read [more here](docs/design.md).

## Features

* Multitenancy
* Roles/permissions system
* Accounts, history, metadata, previews, video/audio calls
* Built-in bouncer, and services
* No config, configuration happens through the admin webif
* Server-side scripting with Python
* Built-in webserver, with a web chat client
* REST API
* Generate invite links

## Server-side scripting

Change messages in transit, modify/add IRCv3 message tags, and more.

More info [docs/modules.md](docs/modules.md)

## Quick start guide

```bash
docker compose build --no-cache
docker compose run --rm chatripper
```

- IRC port: 6667
- Websocket port: 8200
- web-interface: 3000

For more information about Docker: [docs/docker.md](docs/docker.md)

### Compiling

[docs/building.md](docs/building.md).

## Performance

chatripper handles thousands of connections concurrently, even on low-powered hardware.

More info [docs/architecture.md](docs/architecture.md).

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

> &lt;user7&gt; why not using IPFS like made already