#pragma once

#include <cstdint>
#include <optional>
#include <vector>

#include "ob/ladder.hpp"
#include "ob/pool.hpp"
#include "ob/types.hpp"

namespace ob {

enum class Status : std::uint8_t { Ok, InvalidQty, InvalidId, DuplicateId, PriceOutOfRange };

struct AddResult {
    Status status;
    Qty filled;   // quantity matched immediately
    bool rested;  // whether a remainder now rests on the book
};

struct BookConfig {
    std::uint32_t max_orders = 1u << 20;    // resting orders the pool can hold
    std::uint32_t max_order_id = 1u << 20;  // ids must be < this
    LadderConfig ladder{};
};

// Price-time priority limit order book for one instrument.
//
// Hot-path rules: no heap allocation (orders come from a preallocated pool,
// queues are intrusive lists, the id index is a flat vector), no virtual calls
// (trade callbacks are template parameters, not std::function), no floating
// point (prices are integer ticks).
template <template <Side> class Ladder>
class OrderBook {
public:
    explicit OrderBook(const BookConfig& cfg)
        : bids_(cfg.ladder), asks_(cfg.ladder), pool_(cfg.max_orders), by_id_(cfg.max_order_id, nullptr) {}

    // Rests the order at `price`. Matching comes next, so on_trade isn't called yet.
    template <class OnTrade>
    AddResult add_limit(OrderId id, Side side, Price price, Qty qty, OnTrade&& on_trade) {
        if (qty == 0) return {Status::InvalidQty, 0, false};
        if (id >= by_id_.size()) return {Status::InvalidId, 0, false};
        if (by_id_[id]) return {Status::DuplicateId, 0, false};
        if (!bids_.in_range(price)) return {Status::PriceOutOfRange, 0, false};
        return side == Side::Buy ? add_impl<Side::Buy>(id, price, qty, on_trade)
                                 : add_impl<Side::Sell>(id, price, qty, on_trade);
    }

    std::optional<Price> best_bid() { return best_of(bids_); }
    std::optional<Price> best_ask() { return best_of(asks_); }

    std::uint64_t volume_at(Side side, Price p) {
        PriceLevel* lvl = side == Side::Buy ? bids_.find(p) : asks_.find(p);
        return lvl ? lvl->total : 0;
    }

    bool contains(OrderId id) const { return id < by_id_.size() && by_id_[id] != nullptr; }
    std::size_t order_count() const { return live_count_; }
    std::size_t level_count(Side side) const {
        return side == Side::Buy ? bids_.level_count() : asks_.level_count();
    }

private:
    template <Side S, class OnTrade>
    AddResult add_impl(OrderId id, Price price, Qty qty, OnTrade& on_trade) {
        (void)on_trade;
        Qty left = qty;
        AddResult r{Status::Ok, qty - left, false};
        if (left > 0) {
            Order* o = pool_.allocate();
            if (!o) return r;  // pool exhausted: the remainder is dropped
            o->id = id;
            o->price = price;
            o->qty = left;
            o->side = S;
            own<S>().get_or_create(price).push_back(o);
            by_id_[id] = o;
            ++live_count_;
            r.rested = true;
        }
        return r;
    }

    template <Side S>
    auto& own() {
        if constexpr (S == Side::Buy) return bids_;
        else return asks_;
    }

    template <class L>
    static std::optional<Price> best_of(L& ladder) {
        PriceLevel* lvl = ladder.best();
        return lvl ? std::optional<Price>(lvl->price) : std::nullopt;
    }

    Ladder<Side::Buy> bids_;
    Ladder<Side::Sell> asks_;
    Pool<Order> pool_;
    std::vector<Order*> by_id_;  // order id -> resting order, O(1) cancel lookup
    std::size_t live_count_ = 0;
};

using FastBook = OrderBook<ArrayLadder>;
using MapBook = OrderBook<MapLadder>;

}  // namespace ob
