# orderbook

A price-time priority limit order book and matching engine in C++20. It is header-only and has no dependencies.

- **Orders:** limit (match, then rest the remainder), market (immediate-or-cancel), cancel, reduce quantity.
- **Hot path:** no heap allocation, no virtual calls, no floating point.
- **Two ladder implementations** behind one template parameter, benchmarked on identical order flow:
  - `ArrayLadder`: a flat array with one slot per price tick, plus a bitmap of non-empty levels.
  - `MapLadder`: a `std::map` keyed by price.
- **26 tests**, including a differential test that runs both books on 200k random operations and requires identical trades and book state after every operation. The tests also pass under AddressSanitizer and UndefinedBehaviorSanitizer.

## Build

```sh
cmake -S . -B build && cmake --build build -j
./build/tests
./build/bench               # 5M ops, ~10k resting orders
./build/bench 5000000 10000 2048   # ops, target resting orders, depth (wide book)

# with sanitizers
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DOB_SANITIZE=ON && cmake --build build-asan && ./build-asan/tests
```
