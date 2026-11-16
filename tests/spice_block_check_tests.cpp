/**
 * @file    spice_block_check_tests.cpp
 * @brief   Tests for the cache of the pre-run check of blocks kept as SPICE text.
 * @details The benchmark is hidden from the default run; run it with `imcsim-tests "[.benchmark]"`.
 */

#include "backends/nodalys/spice_block_check.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <format>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <thread>
#include <vector>

namespace {

/// Counts the checks that reach the engine and refuses any body that holds a JFET.
struct CountingCheck {
    std::shared_ptr<std::atomic<int>> Calls = std::make_shared<std::atomic<int>>(0);

    Core::SpiceBlockCheck::Checker Make() const {
        return [calls = Calls](const Nodalys::SubcircuitText &text,
                               const std::map<std::string, double> &) -> std::optional<std::string> {
            ++*calls;
            if (text.Body.contains("J1")) {
                return "a JFET";
            }
            return std::nullopt;
        };
    }
};

const Nodalys::SubcircuitText FINE_TEXT{.Name = "AMP", .Ports = {"IN", "OUT"}, .Body = "R1 IN OUT {RTOP}\n"};

} // namespace

TEST_CASE("A block asked twice is checked once", "[spice_block_check]") {
    const CountingCheck counting;
    Core::SpiceBlockCheck cache(counting.Make());
    const std::map<std::string, double> parameters{{"RTOP", 1e3}};

    CHECK_FALSE(cache.Find(FINE_TEXT, parameters).has_value());
    CHECK_FALSE(cache.Find(FINE_TEXT, parameters).has_value());
    CHECK(*counting.Calls == 1);
    CHECK(cache.GetEntryCount() == 1);
}

TEST_CASE("A refusal is remembered with its reason", "[spice_block_check]") {
    const CountingCheck counting;
    Core::SpiceBlockCheck cache(counting.Make());
    const Nodalys::SubcircuitText jfet{.Name = "AMP", .Ports = {"IN", "OUT"}, .Body = "J1 OUT IN 0 JMOD\n"};

    CHECK(cache.Find(jfet, {}) == "a JFET");
    CHECK(cache.Find(jfet, {}) == "a JFET");
    CHECK(*counting.Calls == 1);
}

TEST_CASE("A changed text, port list, name or parameter gets a fresh answer", "[spice_block_check]") {
    const CountingCheck counting;
    Core::SpiceBlockCheck cache(counting.Make());
    const std::map<std::string, double> parameters{{"RTOP", 1e3}};
    CHECK_FALSE(cache.Find(FINE_TEXT, parameters).has_value());

    Nodalys::SubcircuitText edited = FINE_TEXT;
    edited.Body = "J1 OUT IN 0 JMOD\n";
    CHECK(cache.Find(edited, parameters).has_value());
    CHECK(*counting.Calls == 2);

    Nodalys::SubcircuitText renamed = FINE_TEXT;
    renamed.Name = "OTHER";
    CHECK_FALSE(cache.Find(renamed, parameters).has_value());
    CHECK(*counting.Calls == 3);

    Nodalys::SubcircuitText reordered = FINE_TEXT;
    reordered.Ports = {"OUT", "IN"};
    CHECK_FALSE(cache.Find(reordered, parameters).has_value());
    CHECK(*counting.Calls == 4);

    CHECK_FALSE(cache.Find(FINE_TEXT, {{"RTOP", 2e3}}).has_value());
    CHECK(*counting.Calls == 5);

    // The first one is still known
    CHECK_FALSE(cache.Find(FINE_TEXT, parameters).has_value());
    CHECK(*counting.Calls == 5);
    CHECK(cache.GetEntryCount() == 5);
}

TEST_CASE("The cache starts again when it holds too many answers", "[spice_block_check]") {
    const CountingCheck counting;
    Core::SpiceBlockCheck cache(counting.Make());

    for (std::size_t value = 0; value < Core::MAX_SPICE_BLOCK_CHECK_ENTRIES + 10; ++value) {
        static_cast<void>(cache.Find(FINE_TEXT, {{"RTOP", static_cast<double>(value)}}));
        CHECK(cache.GetEntryCount() <= Core::MAX_SPICE_BLOCK_CHECK_ENTRIES);
    }
    // 266 answers fell into a cache that holds 256: it was cleared once, at the 257th, leaving 10
    CHECK(cache.GetEntryCount() == 10);
}

TEST_CASE("Threads asking the same block at once all get the answer", "[spice_block_check]") {
    const CountingCheck counting;
    Core::SpiceBlockCheck cache(counting.Make());
    constexpr int THREAD_COUNT = 4;
    constexpr int ASKS_PER_THREAD = 200;
    std::atomic<int> wrong_answers = 0;

    std::vector<std::jthread> threads;
    for (int thread = 0; thread < THREAD_COUNT; ++thread) {
        threads.emplace_back([&] {
            for (int ask = 0; ask < ASKS_PER_THREAD; ++ask) {
                if (cache.Find(FINE_TEXT, {{"RTOP", 1e3}}).has_value()) {
                    ++wrong_answers;
                }
            }
        });
    }
    threads.clear();

    CHECK(wrong_answers == 0);
    // Threads that met the empty cache together may each have run the check, but never more than once per thread
    CHECK(*counting.Calls <= THREAD_COUNT);
    CHECK(cache.GetEntryCount() == 1);
}

TEST_CASE("The check of Nodalys is cached for a big block", "[.benchmark][spice_block_check]") {
    // A vendor macromodel is a long list of elements; a chain of resistors stands for it
    Nodalys::SubcircuitText big{.Name = "BIG", .Ports = {"IN", "OUT"}, .Body = ""};
    constexpr int ELEMENT_COUNT = 2000;
    for (int index = 0; index < ELEMENT_COUNT; ++index) {
        big.Body += std::format("R{0} N{0} N{1} 1k\n", index, index + 1);
    }
    big.Body += "RA IN N0 1k\nRB N2000 OUT 1k\n";

    constexpr int ASKS = 100;
    const auto time_asks = [&](auto &&ask) {
        const auto start = std::chrono::steady_clock::now();
        for (int index = 0; index < ASKS; ++index) {
            static_cast<void>(ask());
        }
        return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    };
    const double uncached_ms = time_asks([&] { return Nodalys::CheckSubcircuitText(big, {}); });
    const double cached_ms = time_asks([&] { return Core::GetNodalysSpiceBlockCheck().Find(big, {}); });
    std::cout << "SPICE block check, " << ASKS << " asks: uncached " << uncached_ms << " ms, cached " << cached_ms
              << " ms\n";
    CHECK(cached_ms < uncached_ms);
}
