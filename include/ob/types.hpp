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

}  // namespace ob
