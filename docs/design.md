# Design, motivations, goals

chatripper is a chat platform developed on top of IRC. We found 
ways to extend, and improve this [protocol](https://en.wikipedia.org/wiki/IRC) 
in favor of allowing more modern features whilst keeping it backward 
compatible with IRC, and IRCv3 clients.

## The problem

While many existing, centralized chat services provide a modern chat experience, 
self-hosted alternatives like XMPP, Matrix, and others fail to replicate a similar user 
experience effectively.

## Goals

- Multiple connections to a single account
- 'Always online' (bouncer-like functionality)
- Message history
- Advanced role system
  - Roles that define permissions (e.g., moderators can ban but not delete messages)  
  - Per-channel and per-category overrides  
  - Restrict channel visibility to specific roles (e.g., `#mod-chat`, `#admin-chat`)
- Full-text message search
- Message editing
- File transfers that just work
- audio/video calls
- Embedded links
- Server side Python scripting to add or modify behavior
- Avatars, display names, metadata
- Invite links
- Simple protocol
- Built-in webserver, with a web chat client

## Why we choose IRC

Because it is simple, we can quickly debug things using network capture
tools (shoutout to `ngrep -dlo -qt -W byline port 6667`), and the protocol 
is well understood. Other protocols are more complicated, therefor requiring 
more implementation effort. 

IRC (and [IRCv3](https://ircv3.net/)) are *not really* suited for the goals 
outlined above. While IRCv3 is a move in the right direction, the protocol would 
not meet user expectations (see [Mastodon thread](https://web.archive.org/web/20230418155309mp_/https://social.treehouse.systems/@ariadne/110199104168870444)). However, the good 
news is that IRC's simplicity negates the fact that this protocol was 
not designed for our goals. We can simply add protocol features 
(specifications) as we go, in a timely manner.

## Our strategy

We have adopted the following strategy:

1. Do not be 100% IRC spec compliant - but compliant enough for existing clients to work
2. Do not implement all specifications, only if it helps reach our goals
3. When a feature cannot be implemented using an existing specification, we introduce a new specification
4. Try to encapsulate everything over the IRC connection - only go out-of-band when all options are exhausted 
5. Make our own chat client, eventually

## Target audience

#### Users

chatripper aims to be usable for regular, casual internet users. We believe many 
self-hosted chat platforms, and protocols are practically 
only usable for computer-literate people. chatripper will be different - it 
will be for everyone.

#### Operations

We aim to provide a headache-free hosting experience. You turn it on, and It Just Works.

We will provide a Docker compose that will:

1. Run chatripper
2. Not require you to set up TLS certificates, or DNS records
3. Not require you to mess around in +1000 line YAML files (or other types of config files)

All configuration will be done via an admin interface.