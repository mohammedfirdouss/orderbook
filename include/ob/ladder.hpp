#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <type_traits>
#include <vector>

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

template <Side S>
class ArrayLadder {
    static constexpr std::uint32_t kNone = UINT32_MAX;

public:
    explicit ArrayLadder(const LadderConfig& cfg)
        : min_(cfg.min_price), levels_(cfg.num_ticks), bits_((cfg.num_ticks + 63) / 64, 0) {
        for (std::uint32_t i = 0; i < cfg.num_ticks; ++i) levels_[i].price = min_ + static_cast<Price>(i);
    }

    bool in_range(Price p) const {
        return p >= min_ && static_cast<std::int64_t>(p) - min_ < static_cast<std::int64_t>(levels_.size());
    }

    PriceLevel& get_or_create(Price p) {
        std::uint32_t i = index(p);
        PriceLevel& lvl = levels_[i];
        if (lvl.empty()) {
            set_bit(i);
            ++count_;
            if (best_ == kNone || better<S>(p, levels_[best_].price)) best_ = i;
        }
        return lvl;
    }

    PriceLevel* best() { return best_ == kNone ? nullptr : &levels_[best_]; }

    PriceLevel* find(Price p) {
        if (!in_range(p)) return nullptr;
        PriceLevel& lvl = levels_[index(p)];
        return lvl.empty() ? nullptr : &lvl;
    }

    void remove(PriceLevel& lvl) {
        auto i = static_cast<std::uint32_t>(&lvl - levels_.data());
        clear_bit(i);
        --count_;
        // Every better price is already empty, so only search the worse side.
        if (i == best_) best_ = next_best(i);
    }

    std::size_t level_count() const { return count_; }

private:
    std::uint32_t index(Price p) const { return static_cast<std::uint32_t>(p - min_); }
    void set_bit(std::uint32_t i) { bits_[i / 64] |= 1ull << (i % 64); }
    void clear_bit(std::uint32_t i) { bits_[i / 64] &= ~(1ull << (i % 64)); }

    // Walk away from the old best one tick at a time until a level is non-empty.
    std::uint32_t next_best(std::uint32_t i) const {
        if constexpr (S == Side::Buy) {
            while (i-- > 0)
                if (!levels_[i].empty()) return i;
        } else {
            for (++i; i < levels_.size(); ++i)
                if (!levels_[i].empty()) return i;
        }
        return kNone;
    }

    Price min_;
    std::vector<PriceLevel> levels_;
    std::vector<std::uint64_t> bits_;
    std::uint32_t best_ = kNone;
    std::size_t count_ = 0;
};

}  // namespace ob
