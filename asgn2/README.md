# Assignment 2: HTTP Server
**CSE 130 – Principles of Computer Systems Design**

## Overview

`httpserver` is a C program that implements a single-threaded HTTP/1.1 server supporting `GET` and `PUT` requests. The server runs indefinitely, accepting one client connection at a time, processing the request, and closing the connection.

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
./httpserver <port>
```

- `<port>` must be an integer between 1 and 65535
- Ports 1–1023 may require `sudo`
- Prints `Invalid Port\n` to `stderr` and exits with code `1` if the port is invalid or unavailable

---

## Supported Methods

### `GET` — Read a file

Returns the contents of the file identified by the URI.

```bash
curl http://localhost:<port>/foo.txt
```

### `PUT` — Write a file

Creates or overwrites the file identified by the URI with the request body.

```bash
curl http://localhost:<port>/foo.txt -T foo.txt
```

---

## HTTP Status Codes

| Code | Phrase | When |
|------|--------|------|
| 200 | OK | Successful GET or overwrite PUT |
| 201 | Created | PUT creates a new file |
| 400 | Bad Request | Malformed request |
| 403 | Forbidden | File not accessible |
| 404 | Not Found | File does not exist |
| 500 | Internal Server Error | Unexpected server-side failure |
| 501 | Not Implemented | Unrecognized method |
| 505 | Version Not Supported | Non-HTTP/1.1 request |

---

## Implementation Notes

- Handles malformed and malicious requests without crashing
- Uses at most **10 MB of memory** regardless of input
- No memory leaks (verified with `valgrind --leak-check=full`)
- No file descriptor leaks
- Written in **C** (not C++); does not use `FILE *` functions or `system()`

---

## Files

```
asgn2/
├── httpserver.c
├── asgn2_helper_funcs.a
├── Makefile
└── README.md
```

---

## Testing

With `curl`:
```bash
curl http://localhost:1234/foo.txt
curl http://localhost:1234/new.txt -T foo.txt
```

With `cse130_nc` (netcat):
```bash
printf "GET /foo.txt HTTP/1.1\r\n\r\n" | ./cse130_nc localhost 1234
```
