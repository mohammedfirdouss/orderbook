#pragma once

#include <algorithm>
#include <cstdint>
#include <limits>
#include <optional>
#include <vector>

#include "ob/ladder.hpp"
#include "ob/pool.hpp"
#include "ob/types.hpp"

namespace ob {

enum class Status : std::uint8_t { Ok, InvalidQty, InvalidId, DuplicateId, PriceOutOfRange, BookFull, UnknownId };

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

    // Match against the opposite side, then rest any remainder at `price`.
    // on_trade(const Trade&) is called once per fill, in execution order.
    template <class OnTrade>
    AddResult add_limit(OrderId id, Side side, Price price, Qty qty, OnTrade&& on_trade) {
        if (qty == 0) return {Status::InvalidQty, 0, false};
        if (id >= by_id_.size()) return {Status::InvalidId, 0, false};
        if (by_id_[id]) return {Status::DuplicateId, 0, false};
        if (!bids_.in_range(price)) return {Status::PriceOutOfRange, 0, false};
        // Checked up front so an order is never half-filled and then rejected.
        if (pool_.available() == 0) return {Status::BookFull, 0, false};
        return side == Side::Buy ? add_impl<Side::Buy>(id, price, qty, on_trade)
                                 : add_impl<Side::Sell>(id, price, qty, on_trade);
    }

    // Immediate-or-cancel at any price. Unfilled quantity is dropped.
    template <class OnTrade>
    Qty add_market(OrderId taker, Side side, Qty qty, OnTrade&& on_trade) {
        if (qty == 0) return 0;
        Qty left = side == Side::Buy
                       ? match<Side::Buy>(taker, std::numeric_limits<Price>::max(), qty, on_trade)
                       : match<Side::Sell>(taker, std::numeric_limits<Price>::min(), qty, on_trade);
        return qty - left;
    }

    bool cancel(OrderId id) {
        Order* o = lookup(id);
        if (!o) return false;
        PriceLevel* lvl = o->level;
        lvl->erase(o);
        if (lvl->empty()) {
            if (o->side == Side::Buy) bids_.remove(*lvl); else asks_.remove(*lvl);
        }
        free_order(o);
        return true;
    }

    // Shrinking an order keeps its place in the queue. Growing it would jump
    // ahead of later orders unfairly, so that has to be cancel + new.
    bool reduce(OrderId id, Qty new_qty) {
        Order* o = lookup(id);
        if (!o || new_qty >= o->qty) return false;
        if (new_qty == 0) return cancel(id);
        o->level->total -= o->qty - new_qty;
        o->qty = new_qty;
        return true;
    }

    // Change an order's price and/or size. Shrinking at the same price keeps
    // its queue position (via reduce). Any other change is cancel + re-add, so
    // the order goes to the back of its new level and may trade immediately.
    //
    // Everything is validated before the order is touched, so a rejected
    // modify leaves the original order exactly as it was. Re-adding can't hit
    // BookFull: cancelling first frees the order's own pool slot.
    template <class OnTrade>
    AddResult modify(OrderId id, Price new_price, Qty new_qty, OnTrade&& on_trade) {
        Order* o = lookup(id);
        if (!o) return {Status::UnknownId, 0, false};
        if (new_qty == 0) return {Status::InvalidQty, 0, false};  // removing an order is cancel()
        if (!bids_.in_range(new_price)) return {Status::PriceOutOfRange, 0, false};
        if (new_price == o->price && new_qty <= o->qty) {
            if (new_qty < o->qty) reduce(id, new_qty);
            return {Status::Ok, 0, true};
        }
        Side side = o->side;
        cancel(id);
        return add_limit(id, side, new_price, new_qty, on_trade);
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
        Qty left = match<S>(id, price, qty, on_trade);
        AddResult r{Status::Ok, qty - left, false};
        if (left > 0) {
            Order* o = pool_.allocate();  // cannot fail: checked in add_limit
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

    // Walk the opposite side from the best price while it crosses `limit`,
    // filling the oldest order at each level first. Returns unfilled qty.
    template <Side S, class OnTrade>
    Qty match(OrderId taker, Price limit, Qty qty, OnTrade& on_trade) {
        auto& book = opposite<S>();
        while (qty > 0) {
            PriceLevel* lvl = book.best();
            if (!lvl || !crosses<S>(limit, lvl->price)) break;
            while (qty > 0 && lvl->head) {
                Order* maker = lvl->head;
                Qty fill = std::min(qty, maker->qty);
                maker->qty -= fill;
                lvl->total -= fill;
                qty -= fill;
                on_trade(Trade{taker, maker->id, lvl->price, fill});
                if (maker->qty == 0) {
                    lvl->erase(maker);
                    free_order(maker);
                }
            }
            if (lvl->empty()) book.remove(*lvl);
        }
        return qty;
    }

    // A buy crosses asks at or below its limit; a sell crosses bids at or above.
    template <Side S>
    static bool crosses(Price limit, Price resting) {
        if constexpr (S == Side::Buy) return resting <= limit;
        else return resting >= limit;
    }

    template <Side S>
    auto& own() {
        if constexpr (S == Side::Buy) return bids_;
        else return asks_;
    }

    template <Side S>
    auto& opposite() {
        if constexpr (S == Side::Buy) return asks_;
        else return bids_;
    }

    Order* lookup(OrderId id) { return id < by_id_.size() ? by_id_[id] : nullptr; }

    void free_order(Order* o) {
        by_id_[o->id] = nullptr;
        --live_count_;
        pool_.release(o);
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
