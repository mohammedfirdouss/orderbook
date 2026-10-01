#pragma once

#include <cstdint>

namespace ob {

using OrderId = std::uint32_t;
using Price = std::int32_t;  // integer ticks, never floating point
using Qty = std::uint32_t;

enum class Side : std::uint8_t { Buy, Sell };

struct Trade {
    OrderId taker;
    OrderId maker;
    Price price;  // always the resting (maker) order's price
    Qty qty;
};

struct PriceLevel;

// A resting order. prev/next make it a node in its price level's FIFO queue
// (an intrusive list), so queueing an order never allocates.
struct Order {
    Order* prev = nullptr;
    Order* next = nullptr;
    PriceLevel* level = nullptr;
    OrderId id = 0;
    Price price = 0;
    Qty qty = 0;
    Side side = Side::Buy;
};

// All resting orders at one price, oldest first (time priority).
struct PriceLevel {
    Order* head = nullptr;
    Order* tail = nullptr;
    std::uint64_t total = 0;  // sum of qty across the queue
    std::uint32_t count = 0;
    Price price = 0;

    bool empty() const { return head == nullptr; }

    void push_back(Order* o) {
        o->prev = tail;
        o->next = nullptr;
        o->level = this;
        if (tail) tail->next = o; else head = o;
        tail = o;
        total += o->qty;
        ++count;
    }
};

}  // namespace ob
