// Exceptions as a field value, the exception-first call forms of every level and of log(), nested causes and type
// names.

#include <jspdlog/jspdlog.h>

#include "test_support.h"

#include <catch2/catch_test_macros.hpp>

#include <exception>
#include <optional>
#include <stdexcept>
#include <string>
#include <system_error>
#include <vector>

using jspdlog::fields;
using test_support::fixture;

namespace jspdlog_test
{

// The types with a NOLINT below are outside an anonymous namespace, because their logged type names are checked, and
// the name of an anonymous namespace differs between compilers.

// A user type in a named namespace, whose type name is the same on every compiler.
struct payment_error : std::runtime_error // NOLINT(misc-use-internal-linkage): its type name is checked
{
    using std::runtime_error::runtime_error;
};

// A user type that is also a std::nested_exception, as std::throw_with_nested expects such types to be. Built outside a
// handler, it holds no cause.
// NOLINTNEXTLINE(misc-use-internal-linkage,misc-multiple-inheritance): its type name is checked; the mix is the point
struct wrapped_error : std::runtime_error, std::nested_exception
{
    using std::runtime_error::runtime_error;
};

namespace
{

// Counts how often its message is read, to show that a disabled level computes nothing.
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables): counted by what()
int what_calls = 0;
struct counting_error : std::runtime_error
{
    using std::runtime_error::runtime_error;
    [[nodiscard]] const char *what() const noexcept override
    {
        ++what_calls;
        return std::runtime_error::what();
    }
};

} // namespace

// An exception type with its own JSON form, which must win over the exception form. Its type name is checked.
struct my_error : std::runtime_error // NOLINT(misc-use-internal-linkage): its type name is checked
{
    using std::runtime_error::runtime_error;
};

} // namespace jspdlog_test

template <>
struct jspdlog::serializer<jspdlog_test::my_error>
{
    static void write(jspdlog::json_writer &w, const jspdlog_test::my_error &e)
    {
        w.begin_object();
        w.member("custom", std::string{e.what()});
        w.end_object();
    }
};

using test_support::throws_when_formatted;

namespace
{

// The line of an exception under the default keys, up to and including the closing brace of the exception.
std::string error_object(const std::string &type, const std::string &message)
{
    return R"("error":{"type":")" + type + R"(","message":")" + message + R"("})";
}

// The type name that a thrown exception gets, as logged.
template <typename Ex>
std::string logged_type(const Ex &ex)
{
    const fixture f;
    f.logger.error(ex);
    const std::string line = f.last();
    const std::string head = R"({"logger":"t","level":"error","error":{"type":")";
    REQUIRE(line.compare(0, head.size(), head) == 0);
    return line.substr(head.size(), line.find('"', head.size()) - head.size());
}

} // namespace

TEST_CASE(
    "exceptions: the call form writes the exception under error_key, before the other per-call fields", "[exceptions]"
)
{
    const fixture f;
    const std::runtime_error ex("boom");
    f.logger.error(ex, {"host", "db1"}, "connect failed");
    const std::string expected = std::string(R"({"logger":"t","level":"error",)") +
                                 error_object("std::runtime_error", "boom") +
                                 R"(,"host":"db1","message":"connect failed"})";
    CHECK(f.last() == expected);
}

TEST_CASE("exceptions: nested causes are written as a chain", "[exceptions]")
{
    const fixture f;
    try
    {
        try
        {
            throw std::runtime_error("inner");
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("outer"));
        }
    }
    catch (const std::exception &ex)
    {
        f.logger.error(ex);
    }
    const std::string expected =
        R"({"logger":"t","level":"error","error":{"type":"std::logic_error","message":"outer",)"
        R"("cause":{"type":"std::runtime_error","message":"inner"}}})";
    CHECK(f.last() == expected);
}

// clang 23 on Windows, with the MSVC standard library, crashes inside std::throw_with_nested and std::rethrow_if_nested
// for chains of two or more causes, in a program that does not use jspdlog. The chain depth is covered by the other
// toolchains.
#if !(defined(_WIN32) && defined(__clang__))
namespace
{

// Throws an exception that has a chain of n nested causes below it.
// NOLINTNEXTLINE(misc-no-recursion): the recursion builds a chain of n causes
[[noreturn]] void throw_chain(int n)
{
    try
    {
        if (n == 0)
        {
            throw std::runtime_error("root");
        }
        throw_chain(n - 1);
    }
    catch (...)
    {
        std::throw_with_nested(std::runtime_error("level" + std::to_string(n)));
    }
}

std::size_t count_of(const std::string &text, const std::string &part)
{
    std::size_t n = 0;
    for (auto at = text.find(part); at != std::string::npos; at = text.find(part, at + part.size()))
    {
        ++n;
    }
    return n;
}

} // namespace

TEST_CASE("exceptions: a chain is cut at the maximum depth", "[exceptions]")
{
    const fixture f;
    try
    {
        throw_chain(12);
    }
    catch (const std::exception &ex)
    {
        f.logger.error(ex);
    }
    CHECK(count_of(f.last(), R"("cause")") == static_cast<std::size_t>(jspdlog::detail::MAX_EXCEPTION_DEPTH));
    CHECK(jspdlog::detail::MAX_EXCEPTION_DEPTH == 8);
    CHECK(f.last().find("root") == std::string::npos);

    try
    {
        throw_chain(3);
    }
    catch (const std::exception &ex)
    {
        f.logger.error(ex);
    }
    CHECK(count_of(f.last(), R"("cause")") == 4); // level2, level1, level0 and the root
    CHECK(f.last().find(R"("message":"root")") != std::string::npos);
}
#endif

TEST_CASE("exceptions: a cause that is not a std::exception is unknown", "[exceptions]")
{
    const fixture f;
    try
    {
        try
        {
            throw 42; // NOLINT(bugprone-std-exception-baseclass): a cause that is not a std::exception
        }
        catch (...)
        {
            std::throw_with_nested(std::runtime_error("wrapper"));
        }
    }
    catch (const std::exception &ex)
    {
        f.logger.error(ex);
    }
    const std::string expected =
        R"({"logger":"t","level":"error","error":{"type":"std::runtime_error","message":"wrapper",)"
        R"("cause":{"type":"unknown"}}})";
    CHECK(f.last() == expected);
}

TEST_CASE("exceptions: a nested exception that holds no cause is written without one", "[exceptions]")
{
    // std::rethrow_if_nested calls std::terminate for these, because their nested_ptr is null.
    const fixture f;
    SECTION("std::throw_with_nested outside a handler")
    {
        try
        {
            std::throw_with_nested(std::runtime_error("alone"));
        }
        catch (const std::exception &ex)
        {
            f.logger.error(ex);
        }
        CHECK(
            f.last() ==
            std::string(R"({"logger":"t","level":"error",)") + error_object("std::runtime_error", "alone") + "}"
        );
    }
    SECTION("a user type derived from std::nested_exception, built outside a handler")
    {
        const jspdlog_test::wrapped_error ex("alone");
        f.logger.error({"e", ex});
        CHECK(
            f.last() == R"({"logger":"t","level":"error","e":{"type":"jspdlog_test::wrapped_error","message":"alone"}})"
        );
    }
}

TEST_CASE("exceptions: the same type name on every compiler", "[exceptions]")
{
    CHECK(logged_type(std::runtime_error("x")) == "std::runtime_error");
    CHECK(logged_type(std::logic_error("x")) == "std::logic_error");
    CHECK(logged_type(std::out_of_range("x")) == "std::out_of_range");
    CHECK(logged_type(std::system_error(std::make_error_code(std::errc::connection_refused))) == "std::system_error");
    CHECK(logged_type(jspdlog_test::payment_error("x")) == "jspdlog_test::payment_error");
    CHECK(logged_type(std::bad_alloc()) == "std::bad_alloc");
}

TEST_CASE("exceptions: type names are normalized", "[exceptions]")
{
    using jspdlog::detail::normalize_type_name;
    CHECK(normalize_type_name("class std::runtime_error", false) == "std::runtime_error");
    CHECK(normalize_type_name("struct app::err", false) == "app::err");
    CHECK(normalize_type_name("std::__1::system_error", false) == "std::system_error");
    CHECK(normalize_type_name("std::_Nested_exception<std::logic_error>", true) == "std::logic_error");
    CHECK(normalize_type_name("class std::_Nested_exception<class std::logic_error>", true) == "std::logic_error");
    CHECK(normalize_type_name("std::__1::__nested<std::__1::logic_error>", true) == "std::logic_error");
    // Only the library's wrapper is unwrapped, and only for an exception that is a nested_exception.
    CHECK(normalize_type_name("app::tmpl<int>", true) == "app::tmpl<int>");
    CHECK(
        normalize_type_name("std::_Nested_exception<std::logic_error>", false) ==
        "std::_Nested_exception<std::logic_error>"
    );
}

TEST_CASE("exceptions: an exception is a value type, and the call forms match it", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex("boom");
    f.logger.error({"error", ex, "host", "db1"}, "connect failed");
    const std::string by_value = f.last();
    f.logger.error(ex, {"host", "db1"}, "connect failed");
    CHECK(f.last() == by_value);
    f.logger.error(fields{"error", ex});
    const std::string by_fields = f.last();
    f.logger.error(ex);
    CHECK(f.last() == by_fields);
}

TEST_CASE("exceptions: every form on every level and on log()", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex("boom");
    const fields named{"k", 1};
    const std::string text = "a 7";

    const auto check_level = [&](jspdlog::level lvl,
                                 const auto &call_plain,
                                 const auto &call_fmt,
                                 const auto &call_str,
                                 const auto &call_fields,
                                 const auto &call_fields_fmt,
                                 const auto &call_fields_str) {
        f.logger.log(lvl, {"error", ex});
        const std::string none = f.last();
        call_plain();
        CHECK(f.last() == none);

        f.logger.log(lvl, {"error", ex}, "a 7");
        const std::string with_message = f.last();
        call_fmt();
        CHECK(f.last() == with_message);
        call_str();
        CHECK(f.last() == with_message);

        f.logger.log(lvl, {"error", ex, "k", 1});
        const std::string with_fields = f.last();
        call_fields();
        CHECK(f.last() == with_fields);

        f.logger.log(lvl, {"error", ex, "k", 1}, "a 7");
        const std::string with_both = f.last();
        call_fields_fmt();
        CHECK(f.last() == with_both);
        call_fields_str();
        CHECK(f.last() == with_both);
    };

#define JSPDLOG_TEST_EXCEPTION_LEVEL(name, lvl)                                                                        \
    check_level(                                                                                                       \
        lvl,                                                                                                           \
        [&] { f.logger.name(ex); },                                                                                    \
        [&] { f.logger.name(ex, "a {}", 7); },                                                                         \
        [&] { f.logger.name(ex, std::string{"a 7"}); },                                                                \
        [&] { f.logger.name(ex, {"k", 1}); },                                                                          \
        [&] { f.logger.name(ex, {"k", 1}, "a {}", 7); },                                                               \
        [&] { f.logger.name(ex, named, std::string{"a 7"}); }                                                          \
    )
    JSPDLOG_TEST_EXCEPTION_LEVEL(trace, jspdlog::level::trace);
    JSPDLOG_TEST_EXCEPTION_LEVEL(debug, jspdlog::level::debug);
    JSPDLOG_TEST_EXCEPTION_LEVEL(info, jspdlog::level::info);
    JSPDLOG_TEST_EXCEPTION_LEVEL(warn, jspdlog::level::warn);
    JSPDLOG_TEST_EXCEPTION_LEVEL(error, jspdlog::level::err);
    JSPDLOG_TEST_EXCEPTION_LEVEL(critical, jspdlog::level::critical);
#undef JSPDLOG_TEST_EXCEPTION_LEVEL

    // log() takes the same forms with the level first.
    const jspdlog::level lvl = jspdlog::level::warn;
    const auto same = [&](const auto &via_log, const auto &via_value) {
        via_log();
        const std::string a = f.last();
        via_value();
        CHECK(a == f.last());
    };
    same([&] { f.logger.log(lvl, ex); }, [&] { f.logger.warn({"error", ex}); });
    same([&] { f.logger.log(lvl, ex, "a {}", 7); }, [&] { f.logger.warn({"error", ex}, "a 7"); });
    same([&] { f.logger.log(lvl, ex, text); }, [&] { f.logger.warn({"error", ex}, text); });
    same([&] { f.logger.log(lvl, ex, named); }, [&] { f.logger.warn({"error", ex, "k", 1}); });
    same([&] { f.logger.log(lvl, ex, {"k", 1}, "a {}", 7); }, [&] { f.logger.warn({"error", ex, "k", 1}, "a 7"); });
    same([&] { f.logger.log(lvl, ex, {"k", 1}, text); }, [&] { f.logger.warn({"error", ex, "k", 1}, text); });
}

TEST_CASE("exceptions: a single exception, a temporary and a named fields object compile", "[exceptions]")
{
    const fixture f;
    f.logger.error(std::runtime_error("x"));
    const std::string expected =
        std::string(R"({"logger":"t","level":"error",)") + error_object("std::runtime_error", "x") + "}";
    CHECK(f.last() == expected);

    const fields named{"k", 1};
    const std::runtime_error ex("y");
    f.logger.error(ex, named);
    const std::string with_named =
        std::string(R"({"logger":"t","level":"error",)") + error_object("std::runtime_error", "y") + R"(,"k":1})";
    CHECK(f.last() == with_named);
    f.logger.error(ex, named, "m");
    CHECK(f.last().find(R"("k":1,"message":"m")") != std::string::npos);
}

TEST_CASE("exceptions: a disabled level computes nothing", "[exceptions]")
{
    fixture f;
    f.logger.set_level(jspdlog::level::err);
    jspdlog_test::what_calls = 0;
    const jspdlog_test::counting_error ex("boom");

    f.logger.info(ex);
    f.logger.info(ex, "message {}", 1);
    f.logger.info(ex, {"k", 1}, "message");
    f.logger.info({"error", ex}, "message");
    f.logger.log(jspdlog::level::debug, ex);
    CHECK(jspdlog_test::what_calls == 0);
    CHECK(f.sink->lines.empty());

    f.logger.error(ex);
    CHECK(jspdlog_test::what_calls == 1);
    f.logger.error({"error", ex});
    CHECK(jspdlog_test::what_calls == 2);
}

TEST_CASE("exceptions: a lazy value in an exception form runs only when the line is written", "[exceptions]")
{
    fixture f;
    f.logger.set_level(jspdlog::level::err);
    const std::runtime_error ex("boom");
    int runs = 0;
    f.logger.info(ex, {"lazy", [&] { return ++runs; }}, "x");
    CHECK(runs == 0);
    f.logger.error(ex, {"lazy", [&] { return ++runs; }}, "x");
    CHECK(runs == 1);
    CHECK(f.last().find(R"("lazy":1)") != std::string::npos);
}

TEST_CASE("exceptions: a bound field with the error key is replaced at its position", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex("boom");
    f.logger.with({"a", 1, "error", "none", "b", 2}).error(ex, "x");
    const std::string expected = std::string(R"({"logger":"t","level":"error","a":1,)") +
                                 error_object("std::runtime_error", "boom") + R"(,"b":2,"message":"x"})";
    CHECK(f.last() == expected);

    // The bound field stays when the call has no exception.
    f.logger.with({"a", 1, "error", "none"}).error("x");
    CHECK(f.last() == R"({"logger":"t","level":"error","a":1,"error":"none","message":"x"})");
}

TEST_CASE("exceptions: the exception replaces a per-call field with the error key", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex("boom");
    const std::string error = error_object("std::runtime_error", "boom");
    const std::string only = std::string(R"({"logger":"t","level":"error",)") + error + R"(,"message":"x"})";
    f.logger.error(ex, {"error", 1}, "x");
    CHECK(f.last() == only);
    f.logger.error(ex, fields{"error", 1}, "x");
    CHECK(f.last() == only);

    // The exception takes the position of that field.
    const std::string between =
        std::string(R"({"logger":"t","level":"error","a":1,)") + error + R"(,"b":3,"message":"x"})";
    f.logger.error(ex, {"a", 1, "error", 2, "b", 3}, "x");
    CHECK(f.last() == between);
    f.logger.error(ex, fields{"a", 1, "error", 2, "b", 3}, "x");
    CHECK(f.last() == between);
    f.logger.with({"z", 0}).error(ex, {"a", 1, "error", 2, "b", 3}, "x");
    CHECK(
        f.last() == std::string(R"({"logger":"t","level":"error","z":0,"a":1,)") + error + R"(,"b":3,"message":"x"})"
    );
}

TEST_CASE("exceptions: bound fields with the error key from nested with() calls give one exception", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex("boom");
    const auto child = f.logger.with({"error", "outer", "a", 1}).with({"b", 2, "error", "inner"});
    const std::string expected = std::string(R"({"logger":"t","level":"error",)") +
                                 error_object("std::runtime_error", "boom") + R"(,"a":1,"b":2,"c":3,"message":"x"})";
    child.error(ex, {"c", 3}, "x");
    CHECK(f.last() == expected);
    child.error(ex, {"error", "call", "c", 3}, "x");
    CHECK(f.last() == expected);
}

#if JSPDLOG_UTF8_POLICY == JSPDLOG_UTF8_REPLACE // the only policy that writes different keys as the same text
TEST_CASE("exceptions: every field that is written under the error key is replaced", "[exceptions]")
{
    // Two different keys that are not UTF-8 are written as the same text.
    auto opts = test_support::deterministic_options();
    opts.error_key = "\xEF\xBF\xBD";
    const fixture f("t", opts);
    const std::runtime_error ex("boom");
    f.logger.with({"\x80", 1}).error(ex, {"a", 2, "\x81", 3}, "x");
    const std::string expected = std::string("{\"logger\":\"t\",\"level\":\"error\",\"\xEF\xBF\xBD\":") +
                                 R"({"type":"std::runtime_error","message":"boom"},"a":2,"message":"x"})";
    CHECK(f.last() == expected);
}
#endif

TEST_CASE("exceptions: other values do not confuse the placement of the exception", "[exceptions]")
{
    const fixture f;
    const std::runtime_error ex(R"(},"error":[)");
    const std::vector<std::string> nested{"]", R"(,"error":1)"};
    f.logger.error(
        ex, {"raw", jspdlog::raw_json{R"( {"error": [1, "}"]} )"}, "list", nested, "error", ex, "n", -1.5}, "x"
    );
    const std::string expected =
        R"({"logger":"t","level":"error","raw": {"error": [1, "}"]} ,"list":["]",",\"error\":1"],)"
        R"("error":{"type":"std::runtime_error","message":"},\"error\":["},"n":-1.5,"message":"x"})";
    CHECK(f.last() == expected);
}

TEST_CASE("exceptions: a null std::exception_ptr is null and a current exception is written", "[exceptions]")
{
    const fixture f;
    f.logger.info({"e", std::exception_ptr{}});
    CHECK(f.last() == R"({"logger":"t","level":"info","e":null})");

    try
    {
        throw 42; // NOLINT(bugprone-std-exception-baseclass): a cause that is not a std::exception
    }
    catch (...)
    {
        f.logger.info({"e", std::current_exception()});
    }
    CHECK(f.last() == R"({"logger":"t","level":"info","e":{"type":"unknown"}})");

    try
    {
        throw std::runtime_error("boom");
    }
    catch (...)
    {
        f.logger.info({"e", std::current_exception()});
    }
    const std::string expected =
        std::string(R"({"logger":"t","level":"info","e":{"type":"std::runtime_error","message":"boom"}})");
    CHECK(f.last() == expected);
}

TEST_CASE("exceptions: a serializer of a user type wins over the exception form", "[exceptions]")
{
    const fixture f;
    const jspdlog_test::my_error ex("mine");
    f.logger.info({"e", ex});
    CHECK(f.last() == R"({"logger":"t","level":"info","e":{"custom":"mine"}})");
    // The call forms use the exception form of the logger, which is a different path.
    f.logger.error(ex);
    CHECK(f.last().find(R"("type":"jspdlog_test::my_error")") != std::string::npos);
}

TEST_CASE("exceptions: the keys come from the options", "[exceptions]")
{
    auto opts = test_support::deterministic_options();
    opts.error_key = "exception";
    opts.error_type_key = "kind";
    opts.error_message_key = "text";
    opts.error_cause_key = "why";
    const fixture f("t", opts);
    const std::runtime_error ex("boom");
    f.logger.error(ex, {"k", 1});
    CHECK(
        f.last() == R"({"logger":"t","level":"error","exception":{"kind":"std::runtime_error","text":"boom"},"k":1})"
    );

    // A child sees the same keys.
    f.logger.with({"a", 1}).error(ex);
    CHECK(
        f.last() == R"({"logger":"t","level":"error","a":1,"exception":{"kind":"std::runtime_error","text":"boom"}})"
    );
    // So does an exception that is a field value, however it gets into the line.
    const std::string object = R"({"kind":"std::runtime_error","text":"boom"})";
    const std::string value = R"({"logger":"t","level":"info","e":)" + object + "}";
    f.logger.info({"e", ex});
    CHECK(f.last() == value);
    f.logger.info(fields{"e", ex});
    CHECK(f.last() == value);
    f.logger.with({"e", ex}).info(fields{});
    CHECK(f.last() == value);
    f.logger.info({"e", [&] { return std::make_exception_ptr(ex); }});
    CHECK(f.last() == value);
    f.logger.info({"e", [&] { return fields{"inner", ex}; }});
    CHECK(f.last() == R"({"logger":"t","level":"info","e":{"inner":)" + object + "}}");
    f.logger.info({"e", std::vector<std::optional<std::runtime_error>>{ex, std::nullopt}});
    CHECK(f.last() == R"({"logger":"t","level":"info","e":[)" + object + ",null]}");
    try
    {
        throw 42; // NOLINT(bugprone-std-exception-baseclass): an exception that is not a std::exception
    }
    catch (...)
    {
        f.logger.info({"e", std::current_exception()});
    }
    CHECK(f.last() == R"({"logger":"t","level":"info","e":{"kind":"unknown"}})");
}

TEST_CASE("exceptions: the cause key is renamed at every depth", "[exceptions]")
{
    auto opts = test_support::deterministic_options();
    opts.error_cause_key = "caused_by";
    const fixture f("t", opts);
    try
    {
        try
        {
            try
            {
                throw 42; // NOLINT(bugprone-std-exception-baseclass): a cause that is not a std::exception
            }
            catch (...)
            {
                std::throw_with_nested(std::runtime_error("middle"));
            }
        }
        catch (...)
        {
            std::throw_with_nested(std::logic_error("outer"));
        }
    }
// clang 23 on Windows crashes in std::rethrow_if_nested for chains of two or more causes, see above.
#if !(defined(_WIN32) && defined(__clang__))
    catch (const std::exception &ex)
    {
        const std::string object =
            R"({"type":"std::logic_error","message":"outer","caused_by":{"type":"std::runtime_error",)"
            R"("message":"middle","caused_by":{"type":"unknown"}}})";
        f.logger.error(ex);
        CHECK(f.last() == R"({"logger":"t","level":"error","error":)" + object + "}");
        f.logger.error({"e", ex});
        CHECK(f.last() == R"({"logger":"t","level":"error","e":)" + object + "}");
        const fields holder{"e", ex};
        CHECK(
            holder.to_json() ==
            R"({"e":{"type":"std::logic_error","message":"outer","cause":{"type":"std::runtime_error",)"
            R"("message":"middle","cause":{"type":"unknown"}}}})"
        );
    }
#else
    catch (const std::exception &)
    {
    }
#endif
}

namespace
{

// A type whose serializer writes an exception it holds.
struct failure_report
{
    std::runtime_error error;
};

void jspdlog_serialize(jspdlog::json_writer &w, const failure_report &r)
{
    w.begin_object();
    w.member("failure", r.error);
    w.end_object();
}

// A type whose serializer builds text with a writer of its own and passes it on as raw JSON.
struct prebuilt_report
{
    std::runtime_error error;
};

void jspdlog_serialize(jspdlog::json_writer &w, const prebuilt_report &r)
{
    std::string text;
    jspdlog::json_writer own(text);
    own.value(r.error);
    w.raw(text);
}

} // namespace

TEST_CASE("exceptions: nested fields and serializers use the keys of the sink", "[exceptions]")
{
    auto opts = test_support::deterministic_options();
    opts.error_type_key = "kind";
    const fixture f("t", opts);
    const std::runtime_error ex("boom");
    const std::string object = R"({"kind":"std::runtime_error","message":"boom"})";

    const fields inner{"e", ex};
    const fields middle{"inner", inner};
    const fields outer{"middle", middle};
    const std::string nested = R"({"middle":{"inner":{"e":)" + object + "}}}";
    f.logger.info({"outer", outer});
    CHECK(f.last() == R"({"logger":"t","level":"info","outer":)" + nested + "}");
    f.logger.info(fields{"outer", outer});
    CHECK(f.last() == R"({"logger":"t","level":"info","outer":)" + nested + "}");
    // Merged and bound fields keep the markers.
    fields merged{"a", 1};
    merged.merge(inner);
    f.logger.with(merged).info("x");
    CHECK(f.last() == R"({"logger":"t","level":"info","a":1,"e":)" + object + R"(,"message":"x"})");

    f.logger.info({"r", failure_report{ex}});
    CHECK(f.last() == R"({"logger":"t","level":"info","r":{"failure":)" + object + "}}");

    // Text that a serializer makes with its own writer goes who knows where, so it has the default keys.
    f.logger.info({"r", prebuilt_report{ex}});
    CHECK(f.last() == R"({"logger":"t","level":"info","r":{"type":"std::runtime_error","message":"boom"}})");
}

TEST_CASE("exceptions: a writer of your own and to_json write the default keys", "[exceptions]")
{
    const std::runtime_error ex("boom");
    const std::string object = R"({"type":"std::runtime_error","message":"boom"})";

    std::string text;
    jspdlog::json_writer w(text);
    w.value(ex);
    CHECK(text == object);

    const fields inner{"e", ex};
    CHECK(inner.to_json() == R"({"e":)" + object + "}");
    const fields outer{"inner", inner, "n", 1};
    CHECK(outer.to_json() == R"({"inner":{"e":)" + object + R"(},"n":1})");

    std::string nested;
    jspdlog::json_writer n(nested);
    n.value(outer);
    CHECK(nested == outer.to_json());
    CHECK(nested.find('\x01') == std::string::npos);

    fields cleared = inner;
    cleared.clear();
    CHECK(cleared.to_json() == "{}");
}

TEST_CASE("exceptions: a formatter that throws in an exception form goes to the error handler", "[exceptions]")
{
    const fixture f;
    std::vector<std::string> errors;
    f.logger.spdlog_logger()->set_error_handler([&errors](const std::string &msg) { errors.push_back(msg); });
    const std::runtime_error ex("boom");

    CHECK_NOTHROW(f.logger.error(ex, "value {}", throws_when_formatted{}));
    CHECK_NOTHROW(f.logger.error(ex, {"k", 1}, "value {}", throws_when_formatted{}));
    CHECK(f.sink->lines.empty());
    CHECK(errors.size() == 2);
}

TEST_CASE("exceptions: an exception message is escaped", "[exceptions]")
{
    const fixture f;
    f.logger.error(std::runtime_error("a \"quoted\"\nline"));
    const std::string expected = std::string(R"({"logger":"t","level":"error",)") +
                                 R"("error":{"type":"std::runtime_error","message":"a \"quoted\"\nline"}})";
    CHECK(f.last() == expected);
}
