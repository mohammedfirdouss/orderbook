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
