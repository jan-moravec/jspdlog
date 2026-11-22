// Target 1: append_json_quoted. The escaped text must decode back to the input bytes, whatever they are.
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

    // Byte-level oracle: valid for any input, with or without UTF-8.
    FUZZ_CHECK(fuzz::decode_json_string_or_abort(quoted) == in);

    // Independent oracle: a real JSON parser. Until a UTF-8 policy exists (ROADMAP 0.4), invalid UTF-8 passes through
    // and nlohmann rejects it, so this runs on valid input only.
    if (fuzz::is_valid_utf8(in))
    {
        FUZZ_CHECK(nlohmann::json::parse(quoted).get<std::string>() == in);
    }
    return 0;
}
