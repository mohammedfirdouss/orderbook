// Replays one synthetic order stream through both books and reports
// throughput and per-operation latency percentiles.
//
// The stream is generated up front (no RNG inside the timed loop) by driving a
// reference book, so every cancel targets an order that is really resting.

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

#include "ob/order_book.hpp"

using namespace ob;
using Clock = std::chrono::steady_clock;

namespace {

enum class OpType : std::uint8_t { Add, Cancel, Market };

struct Op {
    OpType type;
    Side side;
    OrderId id;
    Price price;
    Qty qty;
};

constexpr Price kMidStart = 1 << 15;
constexpr std::uint32_t kTicks = 1u << 16;

BookConfig make_config(std::size_t num_ops) {
    BookConfig cfg;
    cfg.max_orders = 1u << 20;
    cfg.max_order_id = static_cast<std::uint32_t>(num_ops + 1);
    cfg.ladder = {0, kTicks};
    return cfg;
}

std::vector<Op> generate(std::size_t n, std::uint64_t seed, std::size_t target_live) {
    BookConfig cfg = make_config(n);
    MapBook book(cfg);
    std::mt19937_64 rng(seed);
    std::uniform_real_distribution<double> uni(0.0, 1.0);
    std::exponential_distribution<double> depth(1.0 / 8.0);  // most orders near the touch

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> live;  // resting order ids, for picking cancels
    std::vector<std::uint32_t> pos(n + 1, UINT32_MAX);
    auto forget = [&](OrderId id) {
        std::uint32_t i = pos[id];
        if (i == UINT32_MAX) return;
        pos[live.back()] = i;
        live[i] = live.back();
        live.pop_back();
        pos[id] = UINT32_MAX;
    };
    std::vector<OrderId> filled;
    auto on_trade = [&](const Trade& t) { filled.push_back(t.maker); };

    Price mid = kMidStart;
    OrderId next_id = 0;
    while (ops.size() < n) {
        // Mid price drifts like a random walk, kept inside the ladder's band.
        if (uni(rng) < 0.05) mid += uni(rng) < 0.5 ? -1 : 1;
        mid = std::clamp(mid, Price{kTicks / 4}, Price{3 * kTicks / 4});

        // Below the target depth, adds outnumber cancels so the book fills up;
        // above it, they balance. Market orders stay at 3% throughout.
        bool filling = live.size() < target_live;
        double add_p = filling ? 0.70 : 0.485;
        double cancel_p = filling ? 0.27 : 0.485;
        double r = uni(rng);
        Side side = uni(rng) < 0.5 ? Side::Buy : Side::Sell;
        filled.clear();

        if (live.size() < 100 || r < add_p) {
            Price off;
            if (uni(rng) < 0.10) off = -static_cast<Price>(rng() % 4);  // marketable: crosses the spread
            else off = 1 + static_cast<Price>(depth(rng));
            Price price = side == Side::Buy ? mid - off : mid + off;
            Qty qty = static_cast<Qty>(1 + rng() % 10) * 10;
            OrderId id = next_id++;
            ops.push_back({OpType::Add, side, id, price, qty});
            if (book.add_limit(id, side, price, qty, on_trade).rested) {
                pos[id] = static_cast<std::uint32_t>(live.size());
                live.push_back(id);
            }
        } else if (r < add_p + cancel_p) {
            OrderId id = live[rng() % live.size()];
            ops.push_back({OpType::Cancel, side, id, 0, 0});
            book.cancel(id);
            forget(id);
        } else {
            Qty qty = static_cast<Qty>(10 + rng() % 190);
            ops.push_back({OpType::Market, side, next_id, 0, qty});
            book.add_market(next_id, side, qty, on_trade);
        }
        for (OrderId m : filled)
            if (!book.contains(m)) forget(m);
    }
    return ops;
}

template <class Book>
std::uint64_t apply(Book& book, const Op& op) {
    std::uint64_t traded = 0;
    auto on_trade = [&](const Trade& t) { traded += t.qty; };
    switch (op.type) {
        case OpType::Add: book.add_limit(op.id, op.side, op.price, op.qty, on_trade); break;
        case OpType::Cancel: traded += book.cancel(op.id); break;
        case OpType::Market: book.add_market(op.id, op.side, op.qty, on_trade); break;
    }
    return traded;
}

volatile std::uint64_t g_sink;  // stops the compiler deleting the work

// Untimed per op: the whole replay is timed once, so clock overhead is excluded.
template <class Book>
double throughput_mops(const std::vector<Op>& ops, const BookConfig& cfg) {
    Book book(cfg);
    std::uint64_t sink = 0;
    auto t0 = Clock::now();
    for (const Op& op : ops) sink += apply(book, op);
    auto t1 = Clock::now();
    g_sink = sink;
    double secs = std::chrono::duration<double>(t1 - t0).count();
    return static_cast<double>(ops.size()) / secs / 1e6;
}

struct Percentiles {
    double p50, p99, p999, max;
};

Percentiles percentiles(std::vector<std::uint32_t>& v) {
    if (v.empty()) return {0, 0, 0, 0};
    std::sort(v.begin(), v.end());
    auto at = [&](double q) { return static_cast<double>(v[static_cast<std::size_t>(q * static_cast<double>(v.size() - 1))]); };
    return {at(0.50), at(0.99), at(0.999), static_cast<double>(v.back())};
}

struct LatencyReport {
    Percentiles add, cancel, market;
};

template <class Book>
LatencyReport latency(const std::vector<Op>& ops, const BookConfig& cfg) {
    Book book(cfg);
    std::vector<std::uint32_t> add, cancel, market;
    add.reserve(ops.size());
    cancel.reserve(ops.size());
    market.reserve(ops.size() / 10);
    std::uint64_t sink = 0;
    for (const Op& op : ops) {
        auto t0 = Clock::now();
        sink += apply(book, op);
        auto t1 = Clock::now();
        auto ns = static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        (op.type == OpType::Add ? add : op.type == OpType::Cancel ? cancel : market).push_back(ns);
    }
    g_sink = sink;
    return {percentiles(add), percentiles(cancel), percentiles(market)};
}

// Smallest non-zero step the clock can report.
double clock_resolution_ns() {
    double best = 1e9;
    for (int i = 0; i < 100'000; ++i) {
        auto a = Clock::now();
        auto b = Clock::now();
        while (b == a) b = Clock::now();
        best = std::min(best, std::chrono::duration<double, std::nano>(b - a).count());
    }
    return best;
}

template <class Book>
void report(const char* name, const std::vector<Op>& ops, const BookConfig& cfg, int runs) {
    throughput_mops<Book>(ops, cfg);  // warm-up
    std::vector<double> mops;
    for (int i = 0; i < runs; ++i) mops.push_back(throughput_mops<Book>(ops, cfg));
    std::sort(mops.begin(), mops.end());
    double median = mops[mops.size() / 2];
    LatencyReport lat = latency<Book>(ops, cfg);
    auto row = [](const char* what, const Percentiles& p) {
        std::printf("    %-7s p50 %6.0f   p99 %6.0f   p99.9 %7.0f   max %8.0f ns\n", what, p.p50, p.p99, p.p999, p.max);
    };
    std::printf("%s\n", name);
    std::printf("    throughput  %.1f M ops/s  (%.1f ns/op, median of %d runs)\n", median, 1e3 / median, runs);
    row("add", lat.add);
    row("cancel", lat.cancel);
    row("market", lat.market);
}

}  // namespace

int main(int argc, char** argv) {
    std::size_t n = argc > 1 ? std::strtoull(argv[1], nullptr, 10) : 5'000'000;
    std::size_t target_live = argc > 2 ? std::strtoull(argv[2], nullptr, 10) : 10'000;
    int runs = 5;

    std::vector<Op> ops = generate(n, 7, target_live);
    BookConfig cfg = make_config(n);

    std::size_t adds = 0, cancels = 0, markets = 0;
    for (const Op& op : ops) (op.type == OpType::Add ? adds : op.type == OpType::Cancel ? cancels : markets)++;
    {
        MapBook end_state(cfg);
        for (const Op& op : ops) apply(end_state, op);
        std::printf("ops %zu  (add %.0f%%, cancel %.0f%%, market %.0f%%)  resting at end: %zu orders, %zu+%zu levels\n",
                    ops.size(), 100.0 * static_cast<double>(adds) / static_cast<double>(n),
                    100.0 * static_cast<double>(cancels) / static_cast<double>(n),
                    100.0 * static_cast<double>(markets) / static_cast<double>(n), end_state.order_count(),
                    end_state.level_count(Side::Buy), end_state.level_count(Side::Sell));
    }
    std::printf("clock resolution %.1f ns (per-op latencies below are quantized to this)\n\n", clock_resolution_ns());

    report<FastBook>("ArrayLadder (flat price array + bitmap)", ops, cfg, runs);
    report<MapBook>("MapLadder (std::map)", ops, cfg, runs);
}
