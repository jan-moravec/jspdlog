// A braced list passed to a logging call is kept as references and serialized only when the line is written. These
// tests check that nothing runs for a disabled level, that the line equals the one a fields object gives, that
// temporaries live long enough, and the override rules.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <variant>
#include <vector>

using jspdlog::fields;
using test_support::fixture;

namespace
{

// Counts how often it is serialized.
int counted_serializations = 0;
struct counted
{
    int value;
};
void jspdlog_serialize(jspdlog::json_writer &w, const counted &c)
{
    ++counted_serializations;
    w.value(c.value);
}

struct throws_on_write
{
};
void jspdlog_serialize(jspdlog::json_writer & /*w*/, const throws_on_write & /*value*/)
{
    throw std::runtime_error("serializer failed");
}

enum class shade
{
    dark,
    light,
};
// Unscoped on purpose, as in the value type tests.
// NOLINTNEXTLINE(cppcoreguidelines-use-enum-class)
enum plain_enum
{
    one = 1,
};

struct money
{
    std::int64_t cents;
    std::string currency;
};
void jspdlog_serialize(jspdlog::json_writer &w, const money &m)
{
    w.begin_object();
    w.member("cents", m.cents);
    w.member("currency", m.currency);
    w.end_object();
}

// The line a braced list gives must equal the line a fields object gives, with and without bound context.
template <typename T>
void check_same_line(const T &value)
{
    const fixture f;
    const auto bound = f.logger.with({"service", "api", "k", "bound"});

    f.logger.info({"k", value}, "m");
    const std::string braced = f.last();
    bound.info({"k", value}, "m");
    const std::string braced_bound = f.last();

    fields object;
    object.insert("k", value);
    f.logger.info(object, "m");
    INFO(braced);
    CHECK(f.last() == braced);
    bound.info(object, "m");
    CHECK(f.last() == braced_bound);

    // Fields only, no message.
    f.logger.info({"k", value});
    const std::string braced_only = f.last();
    f.logger.info(object);
    CHECK(f.last() == braced_only);
}

} // namespace

TEST_CASE("lazy: a braced list serializes nothing when the level is off", "[lazy]")
{
    fixture f;
    counted_serializations = 0;

    f.logger.set_level(jspdlog::level::off);
    f.logger.info({"a", counted{1}, "b", counted{2}}, "x");
    f.logger.info({"a", counted{1}});
    f.logger.info({"a", counted{1}}, "x {}", 1);
    f.logger.log(jspdlog::level::err, {"a", counted{1}}, "x");
    CHECK(counted_serializations == 0);
    CHECK(f.sink->lines.empty());

    f.logger.set_level(jspdlog::level::trace);
    f.logger.info({"a", counted{1}, "b", counted{2}}, "x");
    CHECK(counted_serializations == 2);
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":2,"message":"x"})");

    f.logger.set_level(jspdlog::level::warn);
    f.logger.info({"a", counted{1}});
    CHECK(counted_serializations == 2);
}

TEST_CASE("lazy: a braced list gives the same line as a fields object for every value type", "[lazy]")
{
    check_same_line(0);
    check_same_line(-7);
    check_same_line(std::numeric_limits<std::uint64_t>::max());
    check_same_line(std::numeric_limits<std::int64_t>::min());
    check_same_line(1.5);
    check_same_line(2.0F);
    check_same_line(std::numeric_limits<double>::quiet_NaN());
    check_same_line(true);
    check_same_line(nullptr);
    check_same_line("literal \"quoted\"");
    check_same_line(std::string("string\twith tab"));
    check_same_line(std::string_view("view"));
    check_same_line(static_cast<const char *>("pointer"));
    check_same_line(static_cast<const char *>(nullptr));
    check_same_line('c');
    check_same_line(std::optional<int>{5});
    check_same_line(std::optional<int>{});
    check_same_line(shade::light);
    check_same_line(one);
    check_same_line(std::chrono::milliseconds(1500));
    check_same_line(std::chrono::microseconds(1500));
    check_same_line(std::chrono::system_clock::time_point{std::chrono::milliseconds(86400123)});
    check_same_line(std::vector<int>{1, 2, 3});
    check_same_line(std::array<std::string, 2>{{"a", "b"}});
    check_same_line(std::map<std::string, int>{{"a", 1}, {"b", 2}});
    check_same_line(std::map<int, std::string>{{1, "a"}});
    check_same_line(std::tuple<int, std::string>{1, "x"});
    check_same_line(std::pair<int, bool>{1, false});
    check_same_line(std::variant<int, std::string>{"text"});
    check_same_line(std::monostate{});
    check_same_line(jspdlog::raw_json{"[1,2]"});
    check_same_line(jspdlog::raw_json{});
    check_same_line(fields{"a", 1, "b", "x"});
    check_same_line(money{1250, "EUR"});
    check_same_line(std::vector<std::optional<money>>{money{1, "A"}, std::nullopt});
    const int array[3] = {1, 2, 3};
    check_same_line(array);
}

TEST_CASE("lazy: temporaries in a braced list live until the call ends", "[lazy]")
{
    const fixture f;
    const std::string suffix = "-key";
    f.logger.info(
        {std::string("temp") + suffix, std::to_string(42), std::string("k2"), std::string(40, 'x'), "n", counted{3}},
        "{}",
        std::string("message temporary")
    );
    CHECK(
        f.last() == R"({"logger":"t","level":"info","temp-key":"42","k2":"xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx",)"
                    R"("n":3,"message":"message temporary"})"
    );

    const auto bound = f.logger.with({"b", std::string(30, 'y')});
    bound.warn({std::string_view("view-key"), std::vector<int>{1, 2}}, std::string("runtime text"));
    CHECK(
        f.last() == R"({"logger":"t","level":"warning","b":"yyyyyyyyyyyyyyyyyyyyyyyyyyyyyy","view-key":[1,2],)"
                    R"("message":"runtime text"})"
    );
}

TEST_CASE("lazy: a named fields object is a per-call argument", "[lazy]")
{
    const fixture f;
    const fields named{"a", 1, "b", "x"};

    SECTION("lvalue")
    {
        f.logger.info(named);
        CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":"x"})");
        f.logger.info(named, "m");
        CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":"x","message":"m"})");
        f.logger.info(named, "m {}", 5);
        CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":"x","message":"m 5"})");
        f.logger.log(jspdlog::level::debug, named, std::string("text"));
        CHECK(f.last() == R"({"logger":"t","level":"debug","a":1,"b":"x","message":"text"})");
    }
    SECTION("temporary")
    {
        f.logger.info(fields{"c", 2});
        CHECK(f.last() == R"({"logger":"t","level":"info","c":2})");
        f.logger.warn(fields{"c", 2}, "m");
        CHECK(f.last() == R"({"logger":"t","level":"warning","c":2,"message":"m"})");
    }
    SECTION("a sum")
    {
        const fields other{"b", "y", "c", 3};
        f.logger.info(named + other, "m");
        CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":"y","c":3,"message":"m"})");
    }
    SECTION("nested as a value of a braced list")
    {
        f.logger.info({"outer", named, "n", 1});
        CHECK(f.last() == R"({"logger":"t","level":"info","outer":{"a":1,"b":"x"},"n":1})");
    }
    SECTION("an empty fields object and an empty braced list")
    {
        const fields none;
        f.logger.info(none, "m");
        CHECK(f.last() == R"({"logger":"t","level":"info","message":"m"})");
        f.logger.info({}, "m");
        CHECK(f.last() == R"({"logger":"t","level":"info","message":"m"})");
    }
    SECTION("over bound fields")
    {
        const auto bound = f.logger.with({"a", 0, "z", 26});
        bound.info(named);
        CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"z":26,"b":"x"})");
    }
}

TEST_CASE("lazy: a per-call key that is bound keeps the bound position", "[lazy]")
{
    const fixture f;
    const auto bound = f.logger.with({"a", 1, "b", 2, "c", 3});

    bound.info({"d", 4, "b", 20});
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":20,"c":3,"d":4})");

    bound.info({"c", 30, "a", 10}, "m");
    CHECK(f.last() == R"({"logger":"t","level":"info","a":10,"b":2,"c":30,"message":"m"})");

    const auto inner = bound.with({"b", 200, "e", 5});
    inner.info({"e", 50, "f", 6});
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"b":200,"c":3,"e":50,"f":6})");
}

TEST_CASE("lazy: a key that appears twice in one call takes the last value at the first position", "[lazy]")
{
    const fixture f;
    counted_serializations = 0;

    f.logger.info({"x", 1, "y", 2, "x", 3});
    CHECK(f.last() == R"({"logger":"t","level":"info","x":3,"y":2})");

    // Same as a fields object.
    f.logger.info(fields{"x", 1, "y", 2, "x", 3});
    CHECK(f.last() == R"({"logger":"t","level":"info","x":3,"y":2})");

    const auto bound = f.logger.with({"b", 0});
    bound.info({"x", 1, "b", 5, "x", 2, "b", 6});
    CHECK(f.last() == R"({"logger":"t","level":"info","b":6,"x":2})");

    // The earlier value of a repeated key is not serialized.
    f.logger.info({"k", counted{1}, "k", counted{2}, "j", counted{3}});
    CHECK(counted_serializations == 2);
    CHECK(f.last() == R"({"logger":"t","level":"info","k":2,"j":3})");
}

TEST_CASE("lazy: sixteen pairs are the most a braced list holds", "[lazy]")
{
    const fixture f;
    f.logger.info({"k0", 0, "k1", 1, "k2",  2,  "k3",  3,  "k4",  4,  "k5",  5,  "k6",  6,  "k7",  7,
                   "k8", 8, "k9", 9, "k10", 10, "k11", 11, "k12", 12, "k13", 13, "k14", 14, "k15", 15});
    CHECK(
        f.last() == R"({"logger":"t","level":"info","k0":0,"k1":1,"k2":2,"k3":3,"k4":4,"k5":5,"k6":6,"k7":7,"k8":8,)"
                    R"("k9":9,"k10":10,"k11":11,"k12":12,"k13":13,"k14":14,"k15":15})"
    );
    static_assert(jspdlog::detail::call_fields::CAPACITY == 16);
}

TEST_CASE("lazy: a serializer that throws drops the line and reaches the error handler once", "[lazy]")
{
    const fixture f;
    int errors = 0;
    f.logger.spdlog_logger()->set_error_handler([&errors](const std::string & /*message*/) { ++errors; });
    CHECK_NOTHROW(f.logger.info({"a", 1, "t", throws_on_write{}}, "m"));
    CHECK(errors == 1);
    CHECK(f.sink->lines.empty());
    f.logger.info({"a", 1}, "still works");
    CHECK(f.sink->lines.size() == 1);
}
