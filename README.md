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

## Design

```
OrderBook<Ladder>
├── bids_ : Ladder<Buy>    best = highest price
├── asks_ : Ladder<Sell>   best = lowest price
├── pool_ : Pool<Order>    every Order object is allocated once, up front
└── by_id_: vector<Order*> order id -> resting order, for O(1) cancel

PriceLevel  head ⇄ order ⇄ order ⇄ tail   (intrusive FIFO = time priority)
```

| Operation | ArrayLadder | MapLadder |
|---|---|---|
| Add (rests) | O(1) | O(log L) and an allocation if the level is new |
| Cancel | O(1), plus a bitmap scan if it empties the best level | O(1) unlink, plus O(log L) if the level empties |
| Match, per fill | O(1) | O(1), plus O(log L) per emptied level |
| Best bid/ask | O(1), cached | O(1), `begin()` |

The bitmap scan checks 64 price levels per instruction (`std::countl_zero` / `std::countr_zero`), so finding the next best price after the best level empties costs at most `ticks/64` word reads, and usually just one or two.

**Limits:**
- `ArrayLadder` needs a bounded price band (default 65,536 ticks).
- Order ids must be smaller than `max_order_id`, because the id index is a flat array. That fits an exchange that assigns its own ids, but not arbitrary client ids.
- There's no self-trade prevention.
- There's no thread safety. The book is meant to run on one thread per instrument, which is how most matching engines shard.
