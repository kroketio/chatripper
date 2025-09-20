## qircd

Qt6 C++ IRCv3 server for a modern chat experience.

### Goals

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

## Build

```bash
cmake -Bbuild .
make -Cbuild -j4
```

