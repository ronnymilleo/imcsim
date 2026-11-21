/**
 * @file    file_json_tests.cpp
 * @brief   Tests for the JSON helpers shared by the modules that read and write the schematic file.
 */

#include "nlohmann/json.hpp"
#include "schematic/file_json.h"
#include <array>
#include <catch2/catch_test_macros.hpp>
#include <optional>
#include <string>

namespace {

struct Pair {
    double First = 1.0;
    double Second = 2.0;
};

constexpr auto PAIR_KEYS = std::to_array<GUI::ParameterKey<Pair>>({
    {"first", &Pair::First},
    {"second", &Pair::Second},
});

bool IsPositive(const double value) {
    return value > 0.0;
}

} // namespace

TEST_CASE("An optional integer that is missing leaves the target alone", "[file_json]") {
    int target = 7;
    const auto read = GUI::ReadOptionalInt(nlohmann::json::object(), "runs", target, "has no valid runs");
    REQUIRE(read.has_value());
    CHECK_FALSE(*read);
    CHECK(target == 7);
}

TEST_CASE("An optional integer that is there is stored and reported", "[file_json]") {
    int target = 7;
    const auto read = GUI::ReadOptionalInt({{"runs", 12}}, "runs", target, "has no valid runs");
    REQUIRE(read.has_value());
    CHECK(*read);
    CHECK(target == 12);
}

TEST_CASE("An optional integer of the wrong type or outside its limits fails with the given text", "[file_json]") {
    int target = 7;
    for (const nlohmann::json &value : {nlohmann::json("12"),
                                        nlohmann::json(2.5),
                                        nlohmann::json(nullptr),
                                        nlohmann::json(true),
                                        nlohmann::json::array()}) {
        const auto read = GUI::ReadOptionalInt({{"runs", value}}, "runs", target, "has no valid runs");
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error() == "has no valid runs");
    }
    CHECK_FALSE(GUI::ReadOptionalInt({{"runs", 0}}, "runs", target, "too low", 1, 10).has_value());
    CHECK_FALSE(GUI::ReadOptionalInt({{"runs", 11}}, "runs", target, "too high", 1, 10).has_value());
    CHECK(GUI::ReadOptionalInt({{"runs", 10}}, "runs", target, "too high", 1, 10).has_value());
    CHECK(target == 10);
}

TEST_CASE("An optional number takes integers and decimals and obeys the check it is given", "[file_json]") {
    double target = 0.5;
    CHECK(*GUI::ReadOptionalNumber({{"time", 3}}, "time", target, "bad"));
    CHECK(target == 3.0);
    CHECK(*GUI::ReadOptionalNumber({{"time", 0.25}}, "time", target, "bad", &IsPositive));
    CHECK(target == 0.25);

    const auto negative = GUI::ReadOptionalNumber({{"time", -1.0}}, "time", target, "not positive", &IsPositive);
    REQUIRE_FALSE(negative.has_value());
    CHECK(negative.error() == "not positive");
    CHECK_FALSE(GUI::ReadOptionalNumber({{"time", "1"}}, "time", target, "bad").has_value());
    CHECK(target == 0.25);
    CHECK_FALSE(*GUI::ReadOptionalNumber(nlohmann::json::object(), "time", target, "bad"));
}

TEST_CASE("An optional flag and an optional text accept only their own type", "[file_json]") {
    bool flag = false;
    CHECK(*GUI::ReadOptionalBool({{"on", true}}, "on", flag, "bad"));
    CHECK(flag);
    CHECK_FALSE(GUI::ReadOptionalBool({{"on", 1}}, "on", flag, "bad").has_value());
    CHECK_FALSE(*GUI::ReadOptionalBool(nlohmann::json::object(), "on", flag, "bad"));

    std::string text = "before";
    CHECK(*GUI::ReadOptionalString({{"name", "after"}}, "name", text, "bad"));
    CHECK(text == "after");
    CHECK_FALSE(GUI::ReadOptionalString({{"name", 5}}, "name", text, "bad").has_value());
    CHECK_FALSE(*GUI::ReadOptionalString(nlohmann::json::object(), "name", text, "bad"));
    CHECK(text == "after");
}

TEST_CASE("An optional point needs two integers and fills an optional target", "[file_json]") {
    std::optional<GUI::GridPoint> target;
    CHECK_FALSE(*GUI::ReadOptionalPoint(nlohmann::json::object(), "output", target, "bad"));
    CHECK_FALSE(target.has_value());

    CHECK(*GUI::ReadOptionalPoint({{"output", {3, -4}}}, "output", target, "bad"));
    REQUIRE(target.has_value());
    CHECK(target->X == 3);
    CHECK(target->Y == -4);

    for (const nlohmann::json &value :
         {nlohmann::json{1}, nlohmann::json{1, 2, 3}, nlohmann::json{1.5, 2}, nlohmann::json("1,2")}) {
        const auto read = GUI::ReadOptionalPoint({{"output", value}}, "output", target, "has no valid output point");
        REQUIRE_FALSE(read.has_value());
        CHECK(read.error() == "has no valid output point");
    }
}

TEST_CASE("A run of optional reads reports the first error, in order", "[file_json]") {
    CHECK(GUI::FirstError({true, false, true}).has_value());
    CHECK(GUI::FirstError({}).has_value());

    const auto failed =
        GUI::FirstError({true, std::unexpected(std::string("first")), std::unexpected(std::string("second"))});
    REQUIRE_FALSE(failed.has_value());
    CHECK(failed.error() == "first");
}

TEST_CASE("A parameter set written as an object reads back, and a missing object keeps the values", "[file_json]") {
    nlohmann::json object = nlohmann::json::object();
    GUI::WriteParametersObject(PAIR_KEYS, Pair{.First = 10.0, .Second = 20.0}, "pair", object);
    CHECK(object["pair"]["first"] == 10.0);
    CHECK(object["pair"]["second"] == 20.0);

    Pair read;
    REQUIRE(GUI::ReadParametersObject(PAIR_KEYS, object, "pair", "not an object", read).has_value());
    CHECK(read.First == 10.0);
    CHECK(read.Second == 20.0);

    Pair kept;
    REQUIRE(GUI::ReadParametersObject(PAIR_KEYS, nlohmann::json::object(), "pair", "not an object", kept).has_value());
    CHECK(kept.First == 1.0);
    CHECK(kept.Second == 2.0);
}

TEST_CASE("A parameter object that is not an object, or holds a non-number, fails", "[file_json]") {
    Pair parameters;
    const auto not_object =
        GUI::ReadParametersObject(PAIR_KEYS, {{"pair", 5}}, "pair", "has a pair that is bad", parameters);
    REQUIRE_FALSE(not_object.has_value());
    CHECK(not_object.error() == "has a pair that is bad");

    const auto not_number =
        GUI::ReadParametersObject(PAIR_KEYS, {{"pair", {{"second", "x"}}}}, "pair", "unused", parameters);
    REQUIRE_FALSE(not_number.has_value());
    CHECK(not_number.error() == "has no valid second");
}
