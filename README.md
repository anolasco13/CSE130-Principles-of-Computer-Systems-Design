# CSE 130 – Principles of Computer Systems Design
**University of California, Santa Cruz**

Covers the principles governing computer systems design and complexity. Topics include memory and storage, networking, concurrency and synchronization, layering (abstraction and modularity), naming, client-server and virtualized system models, and performance. Requires significant programming projects demonstrating mastery of these concepts.

---

## Repository Structure

Each programming assignment lives in its own directory with a dedicated README describing the problem, approach, and build instructions.

| Assignment | Topic |
|------------|-------|
| `asgn1/` | Command-Line Memory |
| `asgn2/` | HTTP Server |
| `asgn3/` | Concurrent Data Structures |
| `asgn4/` | Multi-Threaded HTTP Server |
| `asgn5/` | Forward HTTP Proxy |

---

## Building & Running

Each assignment can be compiled using the provided `Makefile`:

```bash
cd asgn1
make
./asgn1
```

To clean build artifacts:

```bash
make clean
```

---

## Topics Covered

- Systems design principles and managing complexity
- Memory and storage management
- Networking fundamentals
- Concurrency and synchronization (threads, locks, semaphores)
- Layering, abstraction, and modularity
- Naming and namespaces
- Client-server and virtualized system models
- Performance analysis and optimization

---

## Language & Tools

- **Language:** C
- **Compiler:** `gcc` / `g++`
- **Build system:** `make`
- **Platform:** Linux / Unix (UCSC timeshare or equivalent)

---

*Course materials and assignment specifications are the property of UC Santa Cruz. This repository contains only my own implementations.*
