// Ported from the original HID LoggingTest.cpp (the PropertiesOperations /
// PropertiesTypes / PropertiesPointers tests), adapted to:
//   * the new jspdlog::json_properties API (snake_case)
//   * the new jspdlog::raw_json wrapper for arrays/objects
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

TEST_CASE("json_properties: variadic constructor + addition operators", "[json_properties]")
{
    const jspdlog::json_properties p1{"property1", "abcd", "property2", true};
    REQUIRE(p1.to_string() == R"(,"property1":"abcd","property2":true)");

    const jspdlog::json_properties p2 = p1 + jspdlog::json_properties{"property3", 123};
    REQUIRE(p2.to_string() == R"(,"property1":"abcd","property2":true,"property3":123)");

    const jspdlog::json_properties p3 = jspdlog::json_properties{"property4", 1.23} + p2;
    REQUIRE(p3.to_string() == R"(,"property1":"abcd","property2":true,"property3":123,"property4":1.23)");

    const jspdlog::json_properties p4 =
        jspdlog::json_properties{"property1", 123} + jspdlog::json_properties{"property2", 1.23};
    REQUIRE(p4.to_string() == R"(,"property1":123,"property2":1.23)");
}

TEST_CASE("json_properties: covers all scalar types and raw_json", "[json_properties]")
{
    const jspdlog::json_properties properties{
        "p1",
        std::string("abc"),
        "p2",
        true,
        "p3",
        1.23,
        "p4",
        123,
        "p5",
        -123,
        "p6",
        nullptr,
        "p7",
        jspdlog::raw_json{R"([1,true,"a"])"},
    };
    REQUIRE(
        properties.to_string() == R"(,"p1":"abc","p2":true,"p3":1.23,"p4":123,"p5":-123,"p6":null,"p7":[1,true,"a"])"
    );
}

TEST_CASE("json_properties: null pointers become null", "[json_properties]")
{
    const char *p1 = nullptr;
    const int *p2 = nullptr;
    const bool *p3 = nullptr;
    const double *p4 = nullptr;

    const jspdlog::json_properties properties{"p1", p1, "p2", p2, "p3", p3, "p4", p4};
    REQUIRE(properties.to_string() == R"(,"p1":null,"p2":null,"p3":null,"p4":null)");
}

TEST_CASE("json_properties: non-null pointers are dereferenced", "[json_properties]")
{
    const char *p1 = "abcd";
    const int p2 = 123;
    const bool p3 = false;
    const double p4 = 1.23;

    const jspdlog::json_properties properties{"p1", p1, "p2", &p2, "p3", &p3, "p4", &p4};
    REQUIRE(properties.to_string() == R"(,"p1":"abcd","p2":123,"p3":false,"p4":1.23)");
}

TEST_CASE("json_properties: pointer-to-pointer is recursively dereferenced", "[json_properties]")
{
    SECTION("null at any level")
    {
        const char *const *p1 = nullptr;
        const int *const *p2 = nullptr;
        const bool *const *p3 = nullptr;
        const double *const *p4 = nullptr;

        const jspdlog::json_properties properties{"p1", p1, "p2", p2, "p3", p3, "p4", p4};
        REQUIRE(properties.to_string() == R"(,"p1":null,"p2":null,"p3":null,"p4":null)");
    }

    SECTION("dereferenced through both levels")
    {
        const char *p1 = "abcd";
        const int v2 = 123;
        const bool v3 = false;
        const double v4 = 1.23;

        const int *p2 = &v2;
        const bool *p3 = &v3;
        const double *p4 = &v4;

        const jspdlog::json_properties properties{"p1", &p1, "p2", &p2, "p3", &p3, "p4", &p4};
        REQUIRE(properties.to_string() == R"(,"p1":"abcd","p2":123,"p3":false,"p4":1.23)");
    }
}

TEST_CASE("json_properties: merge keeps the right-hand-side value for duplicate keys", "[json_properties]")
{
    jspdlog::json_properties lhs{"a", 1, "b", 2};
    const jspdlog::json_properties rhs{"b", 99, "c", 3};

    lhs.merge(rhs);
    REQUIRE(lhs.to_string() == R"(,"a":1,"b":99,"c":3)");
}

TEST_CASE("json_properties: insert escapes strings and keys", "[json_properties]")
{
    jspdlog::json_properties properties;
    properties.insert("key\"with\\quote", "value\nwith\tcontrol");
    // The raw string literal is hoisted into a local because MSVC mis-handles
    // stringification of raw strings inside Catch2's REQUIRE macro.
    const std::string expected = R"(,"key\"with\\quote":"value\nwith\tcontrol")";
    REQUIRE(properties.to_string() == expected);
}

TEST_CASE("json_properties: operator+ keeps rhs values on key collisions", "[json_properties]")
{
    // Cover every lvalue/rvalue combination of operator+ to guard against
    // the historical inconsistency where the (const&, &&) overload made lhs
    // win instead of rhs.
    SECTION("lvalue + lvalue")
    {
        const jspdlog::json_properties a{"x", 1};
        const jspdlog::json_properties b{"x", 2};
        REQUIRE((a + b).to_string() == R"(,"x":2)");
    }
    SECTION("rvalue + lvalue")
    {
        const jspdlog::json_properties b{"x", 2};
        REQUIRE((jspdlog::json_properties{"x", 1} + b).to_string() == R"(,"x":2)");
    }
    SECTION("lvalue + rvalue")
    {
        const jspdlog::json_properties a{"x", 1};
        REQUIRE((a + jspdlog::json_properties{"x", 2}).to_string() == R"(,"x":2)");
    }
    SECTION("rvalue + rvalue")
    {
        REQUIRE((jspdlog::json_properties{"x", 1} + jspdlog::json_properties{"x", 2}).to_string() == R"(,"x":2)");
    }
}

TEST_CASE("json_properties: empty has no entries", "[json_properties]")
{
    jspdlog::json_properties properties;
    REQUIRE(properties.empty());
    REQUIRE(properties.to_string().empty());

    properties.insert("k", 1);
    REQUIRE_FALSE(properties.empty());
}

TEST_CASE("json_properties: size reflects distinct keys (rhs-wins replacement)", "[json_properties]")
{
    // size() must match the de-duplicated key count: the variadic constructor
    // takes pairs in order and rhs-wins on collision, so duplicate keys
    // collapse to one stored entry. This matches the to_string() and merge
    // behavior and saves callers from having to count manually.
    jspdlog::json_properties properties;
    REQUIRE(properties.size() == 0);

    properties.insert("a", 1);
    REQUIRE(properties.size() == 1);

    properties.insert("b", 2);
    REQUIRE(properties.size() == 2);

    properties.insert("a", 99);
    REQUIRE(properties.size() == 2);

    const jspdlog::json_properties from_variadic{"x", 1, "y", 2, "x", 3};
    REQUIRE(from_variadic.size() == 2);
}

TEST_CASE("json_properties: clear drops every entry", "[json_properties]")
{
    // After clear() the object must be indistinguishable from a freshly
    // default-constructed one, so callers can recycle a single instance
    // across a hot loop.
    jspdlog::json_properties properties{"a", 1, "b", 2};
    REQUIRE_FALSE(properties.empty());

    properties.clear();
    REQUIRE(properties.empty());
    REQUIRE(properties.size() == 0);
    REQUIRE(properties.to_string().empty());

    properties.insert("c", 3);
    REQUIRE(properties.size() == 1);
    REQUIRE(properties.to_string() == R"(,"c":3)");
}

TEST_CASE("json_properties: non-finite floats serialize as null", "[json_properties]")
{
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    const float fnan = std::numeric_limits<float>::quiet_NaN();

    const jspdlog::json_properties properties{"nan", nan, "neg_inf", -inf, "pos_inf", inf, "fnan", fnan};
    REQUIRE(properties.to_string() == R"(,"fnan":null,"nan":null,"neg_inf":null,"pos_inf":null)");
}

TEST_CASE("json_properties: integer-valued floats keep a trailing decimal", "[json_properties]")
{
    // fmt's default float format emits the shortest round-trip
    // representation, so `1.0` would serialize as `1` and a JSON consumer
    // would then read it back as an integer -- making the producer's choice
    // of float-vs-int invisible downstream. json_properties forces a
    // trailing `.0` so the JSON type stays stable for any finite float.
    const jspdlog::json_properties properties{
        "zero_d",
        0.0,
        "one_d",
        1.0,
        "neg_d",
        -3.0,
        "one_f",
        1.0f,
        "fraction",
        1.5,
    };
    REQUIRE(
        properties.to_string() ==
        R"(,"fraction":1.5,"neg_d":-3.0,"one_d":1.0,"one_f":1.0,"zero_d":0.0)"
    );
}

TEST_CASE("json_properties: negative zero serializes as -0.0", "[json_properties]")
{
    // Negative zero is finite but fmt emits it as "-0" (no decimal). The
    // trailing-decimal step then turns it into "-0.0", preserving both the
    // sign and the JSON-number type. Pinned because this corner falls
    // between the "non-finite -> null" branch and the "integer-valued
    // finite -> append .0" branch and we want a regression net under it.
    const double neg_zero = -0.0;
    const float neg_zero_f = -0.0f;
    const jspdlog::json_properties properties{"d", neg_zero, "f", neg_zero_f};
    REQUIRE(properties.to_string() == R"(,"d":-0.0,"f":-0.0)");
}

TEST_CASE("json_properties: keys accept std::string_view", "[json_properties]")
{
    // std::string_view doesn't implicitly convert to std::string, but the
    // variadic constructor opts back in by converting through an explicit
    // std::string{view}. Callers can therefore mix and match key types.
    using namespace std::string_view_literals;
    const jspdlog::json_properties properties{"plain", 1, "from_view"sv, 2};
    REQUIRE(properties.to_string() == R"(,"from_view":2,"plain":1)");
}

TEST_CASE("json_properties: accepts narrow and wide integer types", "[json_properties]")
{
    const jspdlog::json_properties properties{
        "i16",
        static_cast<std::int16_t>(-32000),
        "u16",
        static_cast<std::uint16_t>(65000),
        "schar",
        static_cast<signed char>(-12),
        "uchar",
        static_cast<unsigned char>(200),
        "short_v",
        static_cast<short>(-7),
        "size",
        static_cast<std::size_t>(1234567890ULL),
    };
    REQUIRE(
        properties.to_string() == R"(,"i16":-32000,"schar":-12,"short_v":-7,"size":1234567890,"u16":65000,"uchar":200)"
    );
}

TEST_CASE("json_properties: plain char is a one-character JSON string", "[json_properties]")
{
    // `char` is the natural type of a single character literal (e.g. 'a'),
    // and almost always meant to be a string-typed value rather than its
    // numeric code point.
    const jspdlog::json_properties properties{"c", 'a', "newline", '\n', "quote", '"'};
    // Hoisted because MSVC mis-handles stringification of raw strings
    // containing embedded quotes inside Catch2's REQUIRE.
    const std::string expected = R"(,"c":"a","newline":"\n","quote":"\"")";
    REQUIRE(properties.to_string() == expected);
}

TEST_CASE("json_properties: signed char and unsigned char remain integers", "[json_properties]")
{
    // signed char / unsigned char are the canonical int8_t / uint8_t types
    // so they're documented as integer-valued; the dedicated string-emitting
    // overload is reserved for plain `char`.
    const jspdlog::json_properties properties{
        "sc", static_cast<signed char>(-5), "uc", static_cast<unsigned char>(200)
    };
    REQUIRE(properties.to_string() == R"(,"sc":-5,"uc":200)");
}

TEST_CASE("json_properties: empty raw_json serializes as null", "[json_properties]")
{
    const jspdlog::json_properties properties{
        "empty_lvalue",
        jspdlog::raw_json{""},
        "empty_rvalue",
        jspdlog::raw_json{std::string{}},
        "nonempty",
        jspdlog::raw_json{"[1]"},
    };
    REQUIRE(properties.to_string() == R"(,"empty_lvalue":null,"empty_rvalue":null,"nonempty":[1])");
}

TEST_CASE(
    "json_properties: append_merged_to matches operator+().to_string() and never appends to nothing",
    "[json_properties]"
)
{
    // append_merged_to is the hot-path equivalent of "(lhs + rhs).to_string()"
    // -- it walks both sorted maps in lockstep and emits straight into the
    // output buffer. The cases below pin the same semantics the operator+
    // tests already cover (rhs wins on collisions, lexicographic key order)
    // and additionally lock in the "appends to an existing string" contract
    // the logger relies on.
    SECTION("disjoint keys are interleaved in sorted order")
    {
        const jspdlog::json_properties lhs{"a", 1, "c", 3};
        const jspdlog::json_properties rhs{"b", 2, "d", 4};
        std::string out;
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == R"(,"a":1,"b":2,"c":3,"d":4)");
    }
    SECTION("rhs wins on every key collision")
    {
        const jspdlog::json_properties lhs{"a", 1, "b", 2, "c", 3};
        const jspdlog::json_properties rhs{"b", 99, "c", 100, "d", 4};
        std::string out;
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == R"(,"a":1,"b":99,"c":100,"d":4)");
    }
    SECTION("empty lhs emits rhs verbatim")
    {
        const jspdlog::json_properties lhs;
        const jspdlog::json_properties rhs{"a", 1, "b", 2};
        std::string out;
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == R"(,"a":1,"b":2)");
    }
    SECTION("empty rhs emits lhs verbatim")
    {
        const jspdlog::json_properties lhs{"a", 1, "b", 2};
        const jspdlog::json_properties rhs;
        std::string out;
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == R"(,"a":1,"b":2)");
    }
    SECTION("both empty leaves the output buffer untouched")
    {
        const jspdlog::json_properties lhs;
        const jspdlog::json_properties rhs;
        std::string out = "preexisting";
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == "preexisting");
    }
    SECTION("appends after existing content rather than overwriting it")
    {
        const jspdlog::json_properties lhs{"a", 1};
        const jspdlog::json_properties rhs{"b", 2};
        std::string out = "HEAD";
        lhs.append_merged_to(out, rhs);
        REQUIRE(out == R"(HEAD,"a":1,"b":2)");
    }
}
