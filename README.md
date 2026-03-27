# LillyLedger

A high-performance C++ matching engine for trading flower commodities. This system processes a stream of incoming buy and sell orders, manages a price-time priority limit order book for five specific instruments, and outputs precise execution reports.

## Build Instructions

```bash
mkdir build && cd build

cmake ..

make

# run it using
./lillyledger

# delete build files using
make clean
```

## Performance

Benchmarked on a release build, averaged over 5 runs.

| Phase  | 10K orders | 1M orders |
|--------|-----------|-----------|
| Parse  | ~5.5 ms   | ~385 ms   |
| Match  | ~7.6 ms   | ~251 ms   |
| Write  | ~7.0 ms   | ~310 ms   |
| **Total**  | **~20 ms** | **~946 ms** |
| Reports generated | 21,756 | 1,000,000 |
| **Throughput** | ~500K orders/sec | ~1.06M orders/sec |

**Test system:** Intel Core i5-12450H (8 cores / 12 threads, up to 4.4 GHz), 12 MB L3 cache, 16 GB RAM, Linux

## Running Tests

```bash
cd tests && mkdir build && cd build

cmake ..

make

ctest
```
