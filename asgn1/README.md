# Assignment 1: Command-line Memory
**CSE 130 – Principles of Computer Systems Design**

---

## Overview

`memory` is a C program that provides a simple **get/set key-value abstraction** over files in a Linux directory. It reads a single command from `stdin`, performs the requested operation on the filesystem, and exits.

---

## Building

```bash
make
```

Produces the `memory` binary. Requires `clang` with flags `-Wall -Wextra -Werror -pedantic`.

```bash
make clean
```

Removes all `.o` files and binaries.

---

## Usage

### `get` — Read a file

```
get
<filename>
```

Writes the contents of `<filename>` in the current directory to `stdout` and exits with code `0`.

```bash
printf "get\nfoo.txt\n" | ./memory
# → Hello from foo
```

### `set` — Write a file

```
set
<filename>
<content_length>
<contents>
```

Writes the first `<content_length>` bytes of `<contents>` to `<filename>` in the current directory (creating or overwriting it), prints `OK` to `stdout`, and exits with code `0`.

```bash
printf "set\nbaz.txt\n12\nHello World!" | ./memory
# → OK
```

---

## Error Handling

| Condition | Output | Exit Code |
|-----------|--------|-----------|
| Unknown command, invalid filename, missing location, extra input on `get`, file not found | `Invalid Command\n` to `stderr` | `1` |
| Any other failure (e.g. write error) | `Operation Failed\n` to `stderr` | `1` |

---

## Implementation Notes

- Buffers all file I/O — uses at most **10 MB of memory** regardless of input size
- **No memory leaks** (verified with `valgrind --leak-check=full`)
- **No file descriptor leaks** — all opened files are closed before exit
- `set` is permissive: accepts fewer or more bytes than `content_length` (writes only what was provided / the first `content_length` bytes respectively)
- Written in **C** (not C++); does not use `fread`, `fwrite`, `fgetc`, `fputc`, `getline`, or `system()`

---

## Files

```
asgn1/
├── memory.c
├── Makefile
└── README.md
```

---

## Testing

Run the local autograder:

```bash
python3 -m autograder.run.submit memory.c Makefile README.md
```

Check for memory leaks manually:

```bash
printf "get\nfoo.txt\n" | valgrind --leak-check=full ./memory
```
