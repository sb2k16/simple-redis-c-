# simple-redis

A simplified in-memory Redis-compatible server written in **C++17**. Supports concurrent TCP clients, key expiration, LRU eviction, lists, sorted sets, pub/sub, and JSON snapshot persistence.

## Build & run

```bash
make build
make run                    # listens on port 6380

# Or directly:
./bin/simple-redis --port 6380 --max-keys 10000 --snapshot data/dump.json
```

## Client

```bash
./bin/redis-client 127.0.0.1:6380 SET mykey hello
./bin/redis-client 127.0.0.1:6380 GET mykey
```

## Tests

```bash
make test
```

## Protocol (RESP-lite)

Commands are sent as RESP arrays of bulk strings:

```
*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n
```

Example with netcat:

```bash
printf '*3\r\n$3\r\nSET\r\n$3\r\nfoo\r\n$3\r\nbar\r\n' | nc localhost 6380
printf '*2\r\n$3\r\nGET\r\n$3\r\nfoo\r\n' | nc localhost 6380
```

## Supported commands

| Command | Description |
|---------|-------------|
| `PING` | Health check |
| `SET key value` | Set string |
| `GET key` | Get string |
| `DEL key [key ...]` | Delete keys |
| `EXPIRE key seconds` | Set TTL |
| `TTL key` | Get remaining TTL |
| `LPUSH` / `RPUSH` / `LPOP` / `RPOP` / `LRANGE` / `LLEN` | Lists |
| `ZADD` / `ZREM` / `ZRANGE` / `ZSCORE` | Sorted sets |
| `SUBSCRIBE` / `UNSUBSCRIBE` / `PUBLISH` | Pub/Sub |
| `SAVE` | Write snapshot to disk |
| `DBSIZE` | Key count |
| `QUIT` | Close connection |

## Architecture

```
include/           Headers
  protocol/        RESP reader/writer
  store/           In-memory hash table + LRU + expiry
  pubsub/          Channel subscriber hub
  persistence/     JSON snapshot I/O
  commands/        Command dispatch
  server/          TCP server + threading
src/               Implementations
client/            Simple RESP CLI
scripts/           Integration smoke tests
```

- **Concurrency:** one `std::thread` per client connection; `std::mutex` protects the shared store
- **LRU:** doubly-linked list tracks access order; evicts when `--max-keys` exceeded
- **Expiration:** background sweeper + check-on-read; `EXPIRE` / `TTL` supported
- **Persistence:** JSON snapshot at `data/dump.json`; auto-save every 30s; final save on SIGINT/SIGTERM

## Flags

| Flag | Default | Description |
|------|---------|-------------|
| `--port` | `6380` | TCP port |
| `--addr` | `0.0.0.0` | Bind address |
| `--max-keys` | `10000` | LRU eviction limit |
| `--snapshot` | `data/dump.json` | Snapshot file |
| `--snapshot-every` | `30` | Auto-save interval (seconds) |

## Limitations

- Strings, lists, sorted sets only
- Single database, no auth/replication
- LRU by key count (not memory bytes)
- Not fully `redis-cli` compatible — use included client or netcat
