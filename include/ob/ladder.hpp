#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <type_traits>

#include "ob/types.hpp"

namespace ob {

struct LadderConfig {
    Price min_price = 0;
    std::uint32_t num_ticks = 1u << 16;  // ArrayLadder only
};

// True if price a is better than price b for side S.
template <Side S>
constexpr bool better(Price a, Price b) {
    if constexpr (S == Side::Buy) return a > b;
    else return a < b;
}

template <Side S>
class MapLadder {
    // begin() is always the best price: highest bid, lowest ask.
    using Cmp = std::conditional_t<S == Side::Buy, std::greater<Price>, std::less<Price>>;

public:
    explicit MapLadder(const LadderConfig&) {}

    bool in_range(Price) const { return true; }

    PriceLevel& get_or_create(Price p) {
        auto [it, inserted] = levels_.try_emplace(p);
        if (inserted) it->second.price = p;
        return it->second;
    }

    PriceLevel* best() { return levels_.empty() ? nullptr : &levels_.begin()->second; }

    PriceLevel* find(Price p) {
        auto it = levels_.find(p);
        return it == levels_.end() ? nullptr : &it->second;
    }

    // Called once a level has no orders left.
    void remove(PriceLevel& lvl) {
        Price p = lvl.price;  // copy: lvl is destroyed by the erase
        levels_.erase(p);
    }

    std::size_t level_count() const { return levels_.size(); }

private:
    std::map<Price, PriceLevel, Cmp> levels_;
};

}  // namespace ob
