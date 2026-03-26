# Assignment 4: Multi-Threaded HTTP Server
**CSE 130 – Principles of Computer Systems Design**
---

## Overview

Extends the HTTP server from Assignment 2 with a **thread-pool architecture** using the concurrent data structures from Assignment 3. The server processes multiple client requests simultaneously while maintaining a coherent, linearizable audit log of all operations.

---

## Building

```bash
make
```

Produces the `httpserver` binary using `clang` with `-Wall -Wextra -Werror -pedantic`.

```bash
make clean
```

Removes all `.o` files and binaries.

---

## Usage

```bash
./httpserver [-t threads] <port>
```

- `<port>` — required; integer between 1 and 65535
- `-t threads` — optional; number of worker threads (default: 4)

---

## Architecture

Uses a **thread-pool** design with two roles:

- **Dispatcher thread** (`main`) — accepts incoming connections and pushes them onto a thread-safe queue
- **Worker threads** — pop connections from the queue and process HTTP requests; idle via condition variable when the queue is empty

Requests to the same URI are synchronized using reader-writer locks to ensure coherency. Requests to different URIs are processed fully concurrently.

---

## Audit Log

For every processed request, one line is written to `stderr`:

```
<Method>,<URI>,<Status-Code>,<RequestID>\n
```

- `<RequestID>` is taken from the optional `Request-Id` header, or `0` if not present
- Log entries are **atomic** — no partial or corrupted lines
- The log defines a valid **linearization** of all requests: if R1 completes before R2 starts, R1 appears before R2 in the log

**Example:**
```
GET,/a.txt,200,1
GET,/b.txt,404,2
PUT,/b.txt,201,3
GET,/b.txt,200,0
```

---

## Ordering Guarantees

- If two requests overlap in time, the server may process them in either order — but the audit log and responses must be **mutually consistent**
- An outside observer cannot distinguish the server's behavior from a single-threaded server processing requests in log order

---

## Implementation Notes

- No busy-waiting — workers block on a condition variable or semaphore when idle
- No memory leaks; server stays within 10 MB at all times
- Does **not** use `flock`, `fcntl(F_SETLK)`, `system()`, or `execve()`
- Written in **C** (not C++)

---

## Files

```
asgn4/
├── httpserver.c
├── asgn4_helper_funcs.a
├── queue.c
├── queue.h
├── rwlock.c
├── rwlock.h
├── connection.h
├── iowrapper.h
├── listener_socket.h
├── protocol.h
├── request.h
├── response.h
├── Makefile
└── README.md
```

---

## Testing

Submit to autograder:
```bash
python3 -m autograder.run.submit httpserver.c Makefile README.md
```

Manual concurrent test with `olivertwist` (from resources repo):
```bash
./olivertwist <config.toml> | ./sherlock
```
