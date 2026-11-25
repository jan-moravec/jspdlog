// Target 1: append_json_quoted. The escaped text must decode to what the UTF-8 policy of the build makes of the input,
// whatever the bytes are: with the default policy that is the input with each maximal ill-formed subpart replaced.
#include "fuzz_support.h"

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    const std::string_view in = fuzz::as_text(data, size);

    std::string out = "prefix";
    jspdlog::detail::append_json_quoted(out, in);
    FUZZ_CHECK(out.compare(0, 6, "prefix") == 0); // appends, never overwrites
    const std::string_view quoted = std::string_view{out}.substr(6);

    // The same on spdlog's buffer, which json_logger and json_formatter use.
    spdlog::memory_buf_t buf;
    jspdlog::detail::append_json_quoted(buf, in);
    FUZZ_CHECK(std::string_view(buf.data(), buf.size()) == quoted);

    // Byte-level oracle with an independent decoder, for every input and not only for valid UTF-8.
    const std::string expected = fuzz::written_text(in);
    FUZZ_CHECK(fuzz::decode_json_string_or_abort(quoted) == expected);

    // Independent oracle: a real JSON parser. It rejects invalid UTF-8, which only the pass policy still writes.
    if (fuzz::parseable(in))
    {
        FUZZ_CHECK(nlohmann::json::parse(quoted).get<std::string>() == expected);
    }
    return 0;
}
