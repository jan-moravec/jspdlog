// Ported from the original HID LoggingTest.cpp (the PropertiesOperations /
// PropertiesTypes / PropertiesPointers tests), adapted to:
//   * the new jspdlog::json_properties API (snake_case)
//   * the new jspdlog::raw_json wrapper for arrays/objects
//   * Catch2 v3 instead of GoogleTest

#include <jspdlog/jspdlog.h>

#include <catch2/catch_test_macros.hpp>

#include <string>

TEST_CASE("json_properties: variadic constructor + addition operators", "[json_properties]")
{
    const jspdlog::json_properties p1{"property1", "abcd", "property2", true};
    REQUIRE(p1.to_string() == R"(,"property1":"abcd","property2":true)");

    const jspdlog::json_properties p2 = p1 + jspdlog::json_properties{"property3", 123};
    REQUIRE(p2.to_string() == R"(,"property1":"abcd","property2":true,"property3":123)");

    const jspdlog::json_properties p3 = jspdlog::json_properties{"property4", 1.23} + p2;
    REQUIRE(p3.to_string() == R"(,"property1":"abcd","property2":true,"property3":123,"property4":1.23)");

    const jspdlog::json_properties p4 = jspdlog::json_properties{"property1", 123} +
                                        jspdlog::json_properties{"property2", 1.23};
    REQUIRE(p4.to_string() == R"(,"property1":123,"property2":1.23)");
}

TEST_CASE("json_properties: covers all scalar types and raw_json", "[json_properties]")
{
    const jspdlog::json_properties properties{
        "p1", std::string("abc"),
        "p2", true,
        "p3", 1.23,
        "p4", 123,
        "p5", -123,
        "p6", nullptr,
        "p7", jspdlog::raw_json{R"([1,true,"a"])"},
    };
    REQUIRE(properties.to_string() ==
            R"(,"p1":"abc","p2":true,"p3":1.23,"p4":123,"p5":-123,"p6":null,"p7":[1,true,"a"])");
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
    jspdlog::json_properties rhs{"b", 99, "c", 3};

    lhs.merge(rhs);
    REQUIRE(lhs.to_string() == R"(,"a":1,"b":99,"c":3)");
}

TEST_CASE("json_properties: insert escapes strings and keys", "[json_properties]")
{
    jspdlog::json_properties properties;
    properties.insert("key\"with\\quote", "value\nwith\tcontrol");
    REQUIRE(properties.to_string() == R"(,"key\"with\\quote":"value\nwith\tcontrol")");
}

TEST_CASE("json_properties: empty has no entries", "[json_properties]")
{
    jspdlog::json_properties properties;
    REQUIRE(properties.empty());
    REQUIRE(properties.to_string().empty());

    properties.insert("k", 1);
    REQUIRE_FALSE(properties.empty());
}
