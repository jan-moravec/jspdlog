/**
 * @file
 * @brief Makes nlohmann::json, nlohmann::ordered_json and every other nlohmann::basic_json a field value.
 *
 * Include it to log a JSON value of nlohmann/json 3.10 or newer as it is, in a braced list, a fields object, a
 * container or a serializer of your own. The value is dumped to compact text and written into the line unchanged, so it
 * is not escaped again, and an nlohmann::ordered_json keeps its order. A discarded value, the result of a failed parse
 * without exceptions, is written as null; a discarded value nested inside another one is not looked for and makes the
 * line invalid.
 *
 * Invalid UTF-8 inside the strings of the value is replaced by U+FFFD when it is dumped. This is jspdlog's default
 * policy, and these values get it whatever JSPDLOG_UTF8_POLICY is, because nlohmann/json cannot escape such bytes or
 * pass them through. JSPDLOG_ESCAPE_LINE_SEPARATORS does not apply to them either.
 *
 * Include this header in every translation unit that logs such values: without it jspdlog has no JSON form for them,
 * and the logging call does not compile.
 *
 * @code
 * #include <jspdlog/nlohmann.h>
 *
 * const nlohmann::json items = {"alpha", "beta"};
 * logger.info({"items", items}, "loaded");
 * // {...,"items":["alpha","beta"],"message":"loaded"}
 * @endcode
 */

#pragma once

#include <jspdlog/jspdlog.h>

#include <nlohmann/json.hpp>

#include <type_traits>

/** @brief Writes any nlohmann::basic_json as its compact text. */
template <typename T>
struct jspdlog::serializer<T, std::enable_if_t<nlohmann::detail::is_basic_json<T>::value>>
{
    /** @brief Dumps the value without indentation, keeping valid UTF-8 as it is. */
    static void write(json_writer &w, const T &j)
    {
        if (j.is_discarded())
        {
            // Its dump is the text <discarded>, which is not JSON.
            w.value(nullptr);
            return;
        }
        const auto text = j.dump(-1, ' ', false, T::error_handler_t::replace);
        w.raw({text.data(), text.size()});
    }
};
