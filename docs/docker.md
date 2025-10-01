## Docker

For everyone too lazy for [building.md](building.md), you can one-shot run 
this using docker compose.

Exposed ports:

- `6667` (IRC)
- `3000` (Web)

### Base image

```bash
docker compose build --no-cache
```

### Running

```bash
docker compose run --rm circa
```