# Assignment 3: Concurrent Data Structures
**CSE 130 – Principles of Computer Systems Design**
---

## Overview

Implements two thread-safe concurrent data structures in C: a **bounded buffer (queue)** and a **reader-writer lock** with configurable priority. Both are compiled as object files to be linked into larger programs.

---

## Building

```bash
make
```

Produces `queue.o` and `rwlock.o` using `clang` with `-Wall -Wextra -Werror -pedantic`.

```bash
make clean
```

Removes all `.o` files.

---

## Components

### Bounded Buffer (`queue.c`)

A thread-safe FIFO queue that stores arbitrary pointers. Blocks on push when full, blocks on pop when empty.

```c
queue_t *queue_new(int size);     // create queue with max capacity
void     queue_delete(queue_t **q); // free and nullify
bool     queue_push(queue_t *q, void *elem);  // enqueue (blocks if full)
bool     queue_pop(queue_t *q, void **elem);  // dequeue (blocks if empty)
```

**Thread-safety guarantees:**
- Items are dequeued in the order they were enqueued (per-producer ordering preserved)
- No consumer ever reads uninitialized data
- No enqueued item is ever lost

---

### Reader-Writer Lock (`rwlock.c`)

A reader-writer lock that allows multiple concurrent readers but only one writer at a time. Supports three priority modes to prevent starvation.

```c
rwlock_t *rwlock_new(PRIORITY p, int n); // create lock with given priority
void      rwlock_delete(rwlock_t **l);   // free and nullify
void      reader_lock(rwlock_t *rw);
void      reader_unlock(rwlock_t *rw);
void      writer_lock(rwlock_t *rw);
void      writer_unlock(rwlock_t *rw);
```

**Priority modes (`PRIORITY`):**

| Mode | Behavior |
|------|----------|
| `READERS` | Readers are preferred over writers under contention |
| `WRITERS` | Writers are preferred over readers under contention |
| `N_WAY` | Allows up to `n` readers between each writer; prevents starvation of both |

---

## Implementation Notes

- No busy-waiting — uses `pthread` mutexes and condition variables
- No memory leaks; `_delete` functions free all resources and nullify the pointer
- No `main` functions in either implementation file
- Written in **C** (not C++); compiled with `-c` flag to produce object files only

---

## Files

```
asgn3/
├── queue.c
├── queue.h
├── rwlock.c
├── rwlock.h
├── Makefile
└── README.md
```

---

## Testing

```bash
python3 -m autograder.run.submit queue.c rwlock.c Makefile README.md
```

Check for memory leaks:
```bash
valgrind --leak-check=full ./<test_binary>
```
