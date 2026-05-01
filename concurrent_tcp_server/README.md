# Concurrent TCP Server (C++17, epoll, POSIX sockets)

This project contains:

- `concureent.cpp`: non-blocking epoll edge-triggered server with fixed-size worker thread pool.
- `baseline_thread_per_conn.cpp`: baseline thread-per-connection echo server.
- `benchmark_client.cpp`: multi-thread benchmark client to measure throughput and latency.

## Features

- Single-process, non-blocking event loop using `epoll` with `EPOLLET`.
- Fixed-size worker pool for request processing.
- Modes: `echo`, `chat`, `http`.
- RAII ownership for file descriptors and task lifetimes (`ScopedFd`, `std::unique_ptr`).
- Eventfd-based wakeup path from worker threads to event loop.

## Build (Linux)

```bash
make
```

Build with sanitizers:

```bash
make asan
```

## Run

### 1) Epoll server

Echo mode:

```bash
./concurrent_server --mode echo --port 9090 --threads 8
```

Chat mode:

```bash
./concurrent_server --mode chat --port 9090 --threads 8
```

HTTP mode:

```bash
./concurrent_server --mode http --port 9090 --threads 8
```

### 2) Baseline server

```bash
./baseline_server 9091
```

## Benchmark

Echo benchmark against epoll server:

```bash
./benchmark_client --mode echo --host 127.0.0.1 --port 9090 --clients 500 --requests 200 --payload ping
```

Echo benchmark against baseline server:

```bash
./benchmark_client --mode echo --host 127.0.0.1 --port 9091 --clients 500 --requests 200 --payload ping
```

HTTP benchmark against epoll server:

```bash
./benchmark_client --mode http --host 127.0.0.1 --port 9090 --clients 300 --requests 200
```

The benchmark prints:

- `throughput_req_per_s`
- `p50_latency_ms`
- `p99_latency_ms`

## Suggested Validation

Valgrind (Linux):

```bash
valgrind --leak-check=full --show-leak-kinds=all ./concurrent_server --mode echo --port 9090 --threads 8
```

ASan build:

```bash
make asan
./concurrent_server_asan --mode echo --port 9090 --threads 8
```

## Resume Template (fill numbers)

- Implemented a non-blocking server using epoll edge-triggered I/O paired with a fixed-size worker thread pool; sustained **[N concurrent connections]** in a single process.
- Followed RAII throughout (`std::unique_ptr`, scoped sockets) to eliminate leaks; verified with Valgrind and ASan under sustained load.
- Achieved **[X req/s]** / **[p99 latency Y ms]** on **[hardware]** versus **[Z req/s]** for a thread-per-connection baseline, a **[factor]x** throughput improvement.
