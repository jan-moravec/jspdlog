// Target 4: foreign_payload. Payloads logged straight on the underlying spdlog::logger, as third-party code would,
// including prefixes of the payload token. Anything that is not a payload of this process must end up escaped inside
// the message field, whatever it looks like. This is the injection test for the payload protocol. A payload with the
// real token and a damaged rest must not make the formatter read out of bounds, whatever flags it carries.

// A fuzzed payload may repeat a header key. The check for that still runs, because it walks the fields, but it must not
// abort a debug build.
#define JSPDLOG_ASSERT(cond, msg) static_cast<void>(cond)

#include "fuzz_support.h"

namespace
{

constexpr std::string_view HEADER = R"({"logger":"t","level":"info")";

// The protocol, written out independently of the header: token, kind, a flags digit from 0 to 3, eight lowercase hex
// digits for the size of the fields, the fields (empty, or each entry starting with a comma), then the message (empty,
// or a quoted string).
struct validated
{
    bool ours = false;
    char flags = '0';
    std::string_view fields;
    std::string_view message;
};

validated validate(std::string_view p, std::string_view token)
{
    static constexpr std::size_t HEADER_SIZE = 32 + 1 + 1 + 8;
    if (p.size() < HEADER_SIZE || p.substr(0, token.size()) != token)
    {
        return {};
    }
    const char kind = p[32];
    const char flags = p[33];
    if (flags < '0' || flags > '3')
    {
        return {};
    }
    std::size_t n = 0;
    for (std::size_t i = 0; i < 8; ++i)
    {
        const char c = p[34 + i];
        const bool digit = c >= '0' && c <= '9';
        const bool lower = c >= 'a' && c <= 'f';
        if (!(digit || lower))
        {
            return {};
        }
        n = (n * 16) + static_cast<std::size_t>(digit ? c - '0' : c - 'a' + 10);
    }
    const std::string_view rest = p.substr(HEADER_SIZE);
    const bool sizes_ok = (kind == 'F' && n == rest.size()) || (kind == 'M' && n < rest.size());
    if (!sizes_ok)
    {
        return {};
    }
    const std::string_view fields = rest.substr(0, n);
    const std::string_view message = rest.substr(n);
    if (!fields.empty() && fields.front() != ',')
    {
        return {};
    }
    const bool message_ok = message.empty() || (message.size() >= 2 && message.front() == '"' && message.back() == '"');
    if (!message_ok)
    {
        return {};
    }
    return {true, flags, fields, message};
}

// The fields with each key marker replaced by the default key it stands for: the byte 1 and one of four letters.
std::string with_default_keys(std::string_view fields)
{
    std::string out;
    for (std::size_t i = 0; i < fields.size(); ++i)
    {
        if (fields[i] != '\x01' || i + 1 == fields.size())
        {
            out += fields[i];
            continue;
        }
        switch (fields[++i])
        {
        case 'E':
            out += "\"error\":";
            break;
        case 'T':
            out += "\"type\":";
            break;
        case 'M':
            out += "\"message\":";
            break;
        case 'C':
            out += "\"cause\":";
            break;
        default:
            out += '\x01';
            out += fields[i];
            break;
        }
    }
    return out;
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));
    const fuzz::harness h("t", fuzz::deterministic_options());

    // The first byte picks a prefix family, the rest is the payload tail.
    const auto &token = jspdlog::detail::process_token();
    const std::string_view token_view{token.data(), token.size()};
    std::string payload;
    const unsigned family = r.byte() % 4U;
    switch (family)
    {
    case 0: // arbitrary bytes
        break;
    case 1: // a proper prefix of the token
        payload.assign(token.data(), r.byte() % token.size());
        break;
    case 2: // the real token, then a fuzzed header
        payload.assign(token.data(), token.size());
        break;
    default: // the fixed part, with a wrong random part
        payload.assign("jspdlog:");
        break;
    }
    payload += r.rest();

    h.sink->lines.clear();
    h.logger.spdlog_logger()->info(payload);
    FUZZ_CHECK(h.sink->lines.size() == 1);
    const std::string &line = h.sink->lines.front();

    const validated v = validate(payload, token_view);
    // Only the real token makes a payload ours. Families 0, 1 and 3 cannot produce it except by guessing 24 random
    // characters, so for them the message branch below must hold.
    FUZZ_CHECK(family == 2 || !v.ours);

    if (v.ours)
    {
        // The fragments are spliced in, because jspdlog trusts its own payloads, so the line is valid JSON only when
        // the fragments are. Only the splicing is checked: unchanged without flags, and with markers replaced with
        // them.
        std::string tail;
        if (!v.message.empty())
        {
            tail += ",\"message\":";
            tail += v.message;
        }
        tail += "}\n";
        const std::string fields = v.flags == '0' ? std::string{v.fields} : with_default_keys(v.fields);
        const std::string expected = std::string{HEADER} + fields + tail;
        if (v.flags == '0' || v.flags == '1')
        {
            FUZZ_CHECK(line == expected);
            return 0;
        }
        // With an exception, fields move or drop out, so only the ends and the size of the line are known.
        FUZZ_CHECK(line.size() >= HEADER.size() + tail.size() && line.size() <= expected.size());
        FUZZ_CHECK(line.compare(0, HEADER.size(), HEADER) == 0);
        FUZZ_CHECK(line.compare(line.size() - tail.size(), tail.size(), tail) == 0);
        return 0;
    }

    // Not ours: the whole payload is the message and nothing else is added.
    static constexpr std::string_view PREFIX = R"({"logger":"t","level":"info","message":)";
    FUZZ_CHECK(line.size() >= PREFIX.size() + 3);
    FUZZ_CHECK(line.compare(0, PREFIX.size(), PREFIX) == 0);
    FUZZ_CHECK(line.compare(line.size() - 2, 2, "}\n") == 0);
    const std::string_view quoted = std::string_view{line}.substr(PREFIX.size(), line.size() - PREFIX.size() - 2);
    const std::string expected_message = fuzz::written_text(payload);
    FUZZ_CHECK(fuzz::decode_json_string_or_abort(quoted) == expected_message);
    fuzz::check_shape(line, "\n");
    if (fuzz::parseable(payload))
    {
        const fuzz::ojson parsed = fuzz::parse_line(line, "\n");
        FUZZ_CHECK((fuzz::keys_of(parsed) == std::vector<std::string>{"logger", "level", "message"}));
        FUZZ_CHECK(parsed.at("message") == fuzz::ojson(expected_message));
    }
    return 0;
}
