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
