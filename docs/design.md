# Design, motivations, goals

While cIRCa uses the IRC protocol, cIRCa is not a typical IRC server, 
rather it is a chat platform that is developed on top of IRC. We found 
ways to extend, and improve this [old protocol](https://en.wikipedia.org/wiki/IRC) 
in favor of allowing more modern features whilst keeping it backward 
compatible with IRC, and IRCv3 clients.

## The problem

While many existing, centralized chat services provide a modern chat experience, 
self-hosted alternatives like XMPP, Matrix, and others fail to replicate a similar user 
experience effectively. 

Some options suffer from one, or a combination of the following issues: 

- Resource hungry, and buckles under light usage (>100 concurrent connections)
- Poor on-boarding, and overall UX
- Time-consuming to install, and configure
- Heavy/bloated protocol
- Poor choice of server technology 

For example, while we *do* like Prosody (XMPP), this ecosystem 
still suffers from an arcane setup procedure, and hard-to-use client 
software for your average regular internet user.

## Goals

Our goals for a self-hosted chat platform are:

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
- Built-in web chat client

## Why we choose IRC

Because it is simple! we can quickly debug things using standard network capture
tools (shoutout to `ngrep -dlo -qt -W byline port 6667`), and the protocol 
is well understood. Other protocols are more complicated, therefor requiring 
more implementation effort. 

The issue however; IRC (and [IRCv3](https://ircv3.net/)) are *not really* suited for the goals 
outlined above. While IRCv3 is a move in the right direction, the protocol would 
not meet user expectations, as explained by long time IRC developer ariadne in
a [Mastodon thread](https://web.archive.org/web/20230418155309mp_/https://social.treehouse.systems/@ariadne/110199104168870444).

If we picked Matrix, or XMPP, and tried to make a server, it 
would take an unrealistic amount of time. We already know we want to implement 
custom features, and that would require a (deep) understanding of the protocol in 
question, which can be quite a bit to absorb. We also need to create a custom 
client to test all these custom server features, again costing time.

We believe IRC's simplicity negates the fact that this protocol was not designed for 
our goals. We can simply add protocol features (specifications) as we go, 
in a timely manner.

## Our strategy

We have adopted the following strategy:

1. Don't care too much about being IRC spec compliant - but be compliant enough for existing clients to reasonably work
2. When a specification is silly, we don't implement it, or introduce a new version for it
3. When a feature cannot be implemented using an existing specification, we introduce a new specification.
4. Make our own chat client, eventually

## Target audience

#### Users

cIRCa aims to be usable for regular, casual internet users. We believe many 
self-hosted chat platforms, and protocols are practically 
only usable for computer-literate people. cIRCa will be different - it 
will be for everyone.

#### Operations

We aim to provide a headache-free hosting experience. You turn it on, and It Just Works.

We will provide a Docker compose that will:

1. One-shot install/run cIRCa
2. Not require you to set up TLS certificates, or DNS records
3. Not require you to mess around in +1000 line YAML files (or other types of config files)

All configuration will be done via an admin interface.