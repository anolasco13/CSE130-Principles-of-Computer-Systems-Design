# Assignment 5: Forward HTTP Proxy
**CSE 130 – Principles of Computer Systems Design**
---

## Overview

`httpproxy` is a forward HTTP caching proxy written in C. It sits between clients and servers, forwarding GET requests on behalf of clients and caching responses to reduce redundant server connections. Supports two cache eviction policies: **FIFO** and **LRU**.

---

## Building

```bash
make
```

Produces the `httpproxy` binary using `clang` with `-Wall -Wextra -Werror -pedantic`.

```bash
make clean
```

Removes all `.o` files and binaries.

---

## Usage

```bash
./httpproxy <port> <mode> <n>
```

| Argument | Description |
|----------|-------------|
| `port` | Port to listen on (1–65535) |
| `mode` | Cache eviction policy: `FIFO` or `LRU` |
| `n` | Max number of cached entries (0–1024); `0` disables caching |

Prints `Invalid Argument` and exits with `EXIT_FAILURE` if any argument is invalid.

---

## How It Works

1. Client sends a GET request to the proxy with a full URL: `http://<host>:<port>/<uri>`
2. Proxy checks its cache for a matching `(host, port, uri)` tuple
   - **Cache hit** — returns the cached response with an added `Cached: True` header
   - **Cache miss** — opens a new connection to the target server, forwards the request, caches the response, and returns it to the client
3. If the cache is full on a miss, the oldest/least-recently-used entry is evicted based on the selected mode

Only responses up to **1 MB** in size are cached.

---

## Cache Eviction Policies

**FIFO** — evicts the entry that was inserted first (oldest insertion time).

**LRU** — evicts the entry that was least recently accessed; a cache hit updates the recency of the entry.

---

## Implementation Notes

- Single-threaded; no concurrency required
- Does not validate requests — only well-formed GET requests with HTTP/1.1 are tested
- Sends `Connection: close` to upstream servers to simplify response handling
- No memory leaks; proxy stays within 1.25 GB at all times
- Does **not** use `system()`, `execve()`, or `flock()`
- Written in **C** (not C++)

---

## Files

```
asgn5/
├── httpproxy.c
├── a5protocol.h
├── iowrapper.h
├── prequest.h
├── listener_socket.h
├── client_socket.h
├── asgn5_helper_funcs.a
├── Makefile
└── README.md
```

---

## Testing

```bash
python3 -m autograder.run.submit httpproxy.c Makefile README.md
```
