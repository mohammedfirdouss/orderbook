// Dependency-free test runner. Every BOOK_TEST runs against both ladders.

#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>

#include "ob/order_book.hpp"

using namespace ob;

namespace {

int g_failures = 0;

struct TestCase {
    std::string name;
    std::function<void()> fn;
};

std::vector<TestCase>& registry() {
    static std::vector<TestCase> tests;
    return tests;
}

struct Registrar {
    Registrar(std::string name, std::function<void()> fn) { registry().push_back({std::move(name), std::move(fn)}); }
};

#define CHECK(cond)                                                                    \
    do {                                                                               \
        if (!(cond)) {                                                                 \
            std::fprintf(stderr, "  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #cond); \
            ++g_failures;                                                              \
        }                                                                              \
    } while (0)

#define BOOK_TEST(name)                                                       \
    template <class Book>                                                     \
    void name();                                                              \
    Registrar reg_array_##name(#name " [array]", [] { name<FastBook>(); });   \
    Registrar reg_map_##name(#name " [map]", [] { name<MapBook>(); });        \
    template <class Book>                                                     \
    void name()

BookConfig small_config() {
    BookConfig cfg;
    cfg.max_orders = 1024;
    cfg.max_order_id = 1024;
    cfg.ladder = {0, 1u << 14};
    return cfg;
}

struct Recorder {
    std::vector<Trade> trades;
    void operator()(const Trade& t) { trades.push_back(t); }
};

bool same_trade(const Trade& a, const Trade& b) {
    return a.taker == b.taker && a.maker == b.maker && a.price == b.price && a.qty == b.qty;
}

auto ignore = [](const Trade&) {};

}  // namespace

BOOK_TEST(empty_book_has_no_prices) {
    Book b(small_config());
    CHECK(!b.best_bid());
    CHECK(!b.best_ask());
    CHECK(b.order_count() == 0);
}

BOOK_TEST(non_crossing_orders_rest) {
    Book b(small_config());
    CHECK(b.add_limit(1, Side::Buy, 99, 10, ignore).rested);
    CHECK(b.add_limit(2, Side::Buy, 98, 10, ignore).rested);
    CHECK(b.add_limit(3, Side::Sell, 101, 10, ignore).rested);
    CHECK(b.add_limit(4, Side::Sell, 102, 10, ignore).rested);
    CHECK(b.best_bid() == 99);
    CHECK(b.best_ask() == 101);
    CHECK(b.order_count() == 4);
    CHECK(b.level_count(Side::Buy) == 2);
}

BOOK_TEST(time_priority_within_a_level) {
    Book b(small_config());
    b.add_limit(1, Side::Sell, 100, 10, ignore);
    b.add_limit(2, Side::Sell, 100, 10, ignore);
    Recorder r;
    AddResult res = b.add_limit(3, Side::Buy, 100, 15, r);
    CHECK(res.filled == 15 && !res.rested);
    CHECK(r.trades.size() == 2);
    CHECK(same_trade(r.trades[0], {3, 1, 100, 10}));
    CHECK(same_trade(r.trades[1], {3, 2, 100, 5}));
    CHECK(!b.contains(1));
    CHECK(b.volume_at(Side::Sell, 100) == 5);
}

BOOK_TEST(price_priority_and_remainder_rests) {
    Book b(small_config());
    b.add_limit(1, Side::Sell, 101, 10, ignore);
    b.add_limit(2, Side::Sell, 100, 10, ignore);
    Recorder r;
    AddResult res = b.add_limit(3, Side::Buy, 101, 25, r);
    CHECK(res.filled == 20 && res.rested);
    CHECK(r.trades.size() == 2);
    CHECK(same_trade(r.trades[0], {3, 2, 100, 10}));  // better price first
    CHECK(same_trade(r.trades[1], {3, 1, 101, 10}));
    CHECK(!b.best_ask());
    CHECK(b.best_bid() == 101);
    CHECK(b.volume_at(Side::Buy, 101) == 5);
}

BOOK_TEST(trades_at_maker_price) {
    Book b(small_config());
    b.add_limit(1, Side::Buy, 100, 10, ignore);
    Recorder r;
    b.add_limit(2, Side::Sell, 90, 4, r);
    CHECK(r.trades.size() == 1 && r.trades[0].price == 100);
}

BOOK_TEST(cancel_updates_best) {
    Book b(small_config());
    b.add_limit(1, Side::Buy, 100, 10, ignore);
    b.add_limit(2, Side::Buy, 95, 10, ignore);
    CHECK(b.cancel(1));
    CHECK(b.best_bid() == 95);
    CHECK(!b.cancel(1));    // already gone
    CHECK(!b.cancel(500));  // never existed
    CHECK(!b.cancel(5000)); // out of id range
    CHECK(b.cancel(2));
    CHECK(!b.best_bid());
    CHECK(b.order_count() == 0);
}

BOOK_TEST(cancel_from_middle_of_queue) {
    Book b(small_config());
    b.add_limit(1, Side::Sell, 100, 10, ignore);
    b.add_limit(2, Side::Sell, 100, 10, ignore);
    b.add_limit(3, Side::Sell, 100, 10, ignore);
    CHECK(b.cancel(2));
    Recorder r;
    b.add_limit(4, Side::Buy, 100, 30, r);
    CHECK(r.trades.size() == 2);
    CHECK(r.trades[0].maker == 1 && r.trades[1].maker == 3);
}

BOOK_TEST(reduce_keeps_queue_position) {
    Book b(small_config());
    b.add_limit(1, Side::Sell, 100, 10, ignore);
    b.add_limit(2, Side::Sell, 100, 10, ignore);
    CHECK(b.reduce(1, 3));
    CHECK(b.volume_at(Side::Sell, 100) == 13);
    Recorder r;
    b.add_limit(3, Side::Buy, 100, 5, r);
    CHECK(r.trades.size() == 2);
    CHECK(same_trade(r.trades[0], {3, 1, 100, 3}));
    CHECK(same_trade(r.trades[1], {3, 2, 100, 2}));
}

BOOK_TEST(reduce_rejects_increase_and_zero_cancels) {
    Book b(small_config());
    b.add_limit(1, Side::Buy, 100, 10, ignore);
    CHECK(!b.reduce(1, 10));
    CHECK(!b.reduce(1, 11));
    CHECK(b.reduce(1, 0));
    CHECK(!b.contains(1));
    CHECK(!b.best_bid());
}

int main() {
    int failed_tests = 0;
    for (const auto& t : registry()) {
        int before = g_failures;
        t.fn();
        bool ok = g_failures == before;
        if (!ok) ++failed_tests;
        std::printf("%s %s\n", ok ? "PASS" : "FAIL", t.name.c_str());
    }
    std::printf("\n%zu tests, %d failed\n", registry().size(), failed_tests);
    return failed_tests == 0 ? 0 : 1;
}
