// Target 2: fields. A byte-coded sequence of operations runs on jspdlog::fields and on a reference model, then the
// result is compared as a set, as bound context and as a log line. The model is the specification of the semantics:
// keys keep the position of their first insertion, a repeated key replaces the value, and a merge lets the right-hand
// side win.
#include "fuzz_support.h"

#include <cmath>

namespace
{

using fuzz::ojson;

// Keys the logger writes itself in this target. A fuzzed field key must not equal one (duplicate keys are the caller's
// problem), so a clash gets underscores appended.
const std::vector<std::string> RESERVED_KEYS = {"logger", "level", "message"};

// The reference: an insertion-ordered list of key and JSON value.
struct model
{
    std::vector<std::pair<std::string, ojson>> items;

    void put(const std::string &key, ojson value)
    {
        for (auto &item : items)
        {
            if (item.first == key)
            {
                item.second = std::move(value);
                return;
            }
        }
        items.emplace_back(key, std::move(value));
    }

    void merge(const model &other)
    {
        for (const auto &item : other.items)
        {
            put(item.first, item.second);
        }
    }

    [[nodiscard]] ojson to_json() const
    {
        ojson object = ojson::object();
        for (const auto &item : items)
        {
            object[item.first] = item.second;
        }
        return object;
    }
};

struct built
{
    jspdlog::fields f;
    model m;
};

class builder
{
public:
    explicit builder(fuzz::reader &reader)
        : r_(reader)
    {
    }

    // False once any key or string value was not valid UTF-8: the line cannot be parsed then, only shape-checked.
    bool all_utf8 = true;

    // Reads up to max_ops operations. A merge or a nested value reads a smaller set recursively.
    built build(unsigned depth, unsigned max_ops)
    {
        built out;
        const unsigned count = static_cast<unsigned>(r_.byte()) % (max_ops + 1);
        for (unsigned i = 0; i < count; ++i)
        {
            apply_(out, depth);
        }
        return out;
    }

private:
    std::string key_(bool avoid_reserved)
    {
        // Most keys come from a tiny alphabet so that inserts collide and exercise replacement.
        const std::uint8_t b = r_.byte();
        std::string k = b < 128U ? std::string(1, static_cast<char>('a' + (b % 4U))) : r_.string(8);
        if (avoid_reserved)
        {
            k = fuzz::avoid_reserved_key(std::move(k), RESERVED_KEYS);
        }
        all_utf8 = all_utf8 && fuzz::is_valid_utf8(k);
        return k;
    }

    void apply_(built &out, unsigned depth)
    {
        const unsigned op = static_cast<unsigned>(r_.byte()) % 6U;
        switch (op)
        {
        case 0:
        {
            const std::string k = key_(depth == 0);
            const std::string v = r_.string(16);
            all_utf8 = all_utf8 && fuzz::is_valid_utf8(v);
            out.f.insert(k, std::string_view{v});
            out.m.put(k, ojson(v));
            break;
        }
        case 1:
        {
            const std::string k = key_(depth == 0);
            const unsigned width = static_cast<unsigned>(r_.byte()) % 8U;
            const auto bits = r_.value<std::uint64_t>();
            switch (width)
            {
            case 0:
                out.f.insert(k, static_cast<std::int8_t>(bits));
                out.m.put(k, ojson(static_cast<std::int64_t>(static_cast<std::int8_t>(bits))));
                break;
            case 1:
                out.f.insert(k, static_cast<std::int16_t>(bits));
                out.m.put(k, ojson(static_cast<std::int64_t>(static_cast<std::int16_t>(bits))));
                break;
            case 2:
                out.f.insert(k, static_cast<std::int32_t>(bits));
                out.m.put(k, ojson(static_cast<std::int64_t>(static_cast<std::int32_t>(bits))));
                break;
            case 3:
                out.f.insert(k, static_cast<std::int64_t>(bits));
                out.m.put(k, ojson(static_cast<std::int64_t>(bits)));
                break;
            case 4:
                out.f.insert(k, static_cast<std::uint8_t>(bits));
                out.m.put(k, ojson(static_cast<std::uint64_t>(static_cast<std::uint8_t>(bits))));
                break;
            case 5:
                out.f.insert(k, static_cast<std::uint16_t>(bits));
                out.m.put(k, ojson(static_cast<std::uint64_t>(static_cast<std::uint16_t>(bits))));
                break;
            case 6:
                out.f.insert(k, static_cast<std::uint32_t>(bits));
                out.m.put(k, ojson(static_cast<std::uint64_t>(static_cast<std::uint32_t>(bits))));
                break;
            default:
                out.f.insert(k, bits);
                out.m.put(k, ojson(bits));
                break;
            }
            break;
        }
        case 2:
        {
            const std::string k = key_(depth == 0);
            const auto d = r_.value<double>();
            out.f.insert(k, d);
            out.m.put(k, std::isfinite(d) ? ojson(d) : ojson(nullptr));
            break;
        }
        case 3:
        {
            const std::string k = key_(depth == 0);
            switch (r_.byte() % 4U)
            {
            case 0:
                out.f.insert(k, true);
                out.m.put(k, ojson(true));
                break;
            case 1:
                out.f.insert(k, false);
                out.m.put(k, ojson(false));
                break;
            case 2:
                out.f.insert(k, nullptr);
                out.m.put(k, ojson(nullptr));
                break;
            default:
            {
                const char *none = nullptr;
                out.f.insert(k, none);
                out.m.put(k, ojson(nullptr));
                break;
            }
            }
            break;
        }
        case 4:
        {
            const std::string k = key_(depth == 0);
            const built nested = depth < MAX_DEPTH ? build(depth + 1, 4) : built{};
            out.f.insert(k, nested.f);
            out.m.put(k, nested.m.to_json());
            break;
        }
        default:
        {
            const built other = build(depth, 3);
            out.f.merge(other.f);
            out.m.merge(other.m);
            break;
        }
        }
    }

    static constexpr unsigned MAX_DEPTH = 4;
    fuzz::reader &r_;
};

// The fields as serialized, parsed back, must equal the model, in the same order.
void check_set(const built &b, bool utf8)
{
    FUZZ_CHECK(b.f.size() == b.m.items.size());
    FUZZ_CHECK(b.f.empty() == b.m.items.empty());
    const std::string text = b.f.to_json();
    if (!utf8)
    {
        FUZZ_CHECK(text.size() >= 2 && text.front() == '{' && text.back() == '}');
        for (const char c : text)
        {
            FUZZ_CHECK(static_cast<unsigned char>(c) >= 0x20U);
        }
        return;
    }
    const ojson parsed = ojson::parse(text, nullptr, false);
    FUZZ_CHECK(parsed.is_object());
    FUZZ_CHECK(parsed.dump() == b.m.to_json().dump());
    for (const auto &item : b.m.items)
    {
        FUZZ_CHECK(b.f.contains(item.first));
    }
}

// A braced list gives byte for byte the line that a fields object built from the same pairs gives, with the same
// override rules: bound keys keep their position and a repeated key takes the last value at the first position.
void check_braced_list(fuzz::reader &r, const jspdlog::json_logger &logger, std::vector<std::string> &lines)
{
    const auto next_key = [&r] {
        const std::uint8_t b = r.byte();
        const std::string k = b < 128U ? std::string(1, static_cast<char>('a' + (b % 4U))) : r.string(8);
        return fuzz::avoid_reserved_key(k, RESERVED_KEYS);
    };
    const std::string k0 = next_key();
    const std::string k1 = next_key();
    const std::string k2 = next_key();
    const std::string k3 = next_key();
    const auto number = r.value<std::int64_t>();
    const std::string text = r.string(12);
    const auto real = r.value<double>();
    const std::optional<std::int64_t> maybe = (r.byte() & 1U) != 0 ? std::optional<std::int64_t>{number} : std::nullopt;
    const bool with_message = (r.byte() & 1U) != 0;

    jspdlog::fields object;
    object.insert(k0, number);
    object.insert(k1, text);
    object.insert(k2, real);
    object.insert(k3, maybe);

    lines.clear();
    if (with_message)
    {
        logger.info({k0, number, k1, text, k2, real, k3, maybe}, "m {}", 1);
        logger.info(object, "m {}", 1);
    }
    else
    {
        logger.info({k0, number, k1, text, k2, real, k3, maybe});
        logger.info(object);
    }
    FUZZ_CHECK(lines.size() == 2);
    FUZZ_CHECK(lines[0] == lines[1]);
}

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));
    builder b(r);

    const built a = b.build(0, 12); // bound to a child logger
    const built c = b.build(0, 6);  // bound to a grandchild
    const built d = b.build(0, 6);  // passed per call
    const bool with_message = (r.byte() & 1U) != 0;
    const std::string message = r.string(24);
    const bool utf8 = b.all_utf8 && fuzz::is_valid_utf8(message);

    // 1. Each set on its own, and the operators that combine sets.
    check_set(a, utf8);
    check_set(c, utf8);
    check_set(d, utf8);
    {
        built sum;
        sum.f = a.f + c.f;
        sum.m = a.m;
        sum.m.merge(c.m);
        check_set(sum, utf8);
    }

    // 2. Bound context: with() merges, and the original logger does not change.
    const fuzz::harness h("t", fuzz::deterministic_options());
    const jspdlog::json_logger child = h.logger.with(a.f);
    const jspdlog::json_logger grandchild = child.with(c.f);
    FUZZ_CHECK(h.logger.context().empty());
    model bound = a.m;
    FUZZ_CHECK(child.context().size() == bound.items.size());
    bound.merge(c.m);
    FUZZ_CHECK(grandchild.context().size() == bound.items.size());

    // 3. The line: header, then bound fields, then per-call fields, then the message. A per-call key that is bound
    // replaces the value at the bound position; a new key goes last.
    model line = bound;
    line.merge(d.m);
    if (with_message)
    {
        grandchild.info(d.f, message);
    }
    else
    {
        grandchild.info(d.f);
    }
    FUZZ_CHECK(h.sink->lines.size() == 1);
    const std::string text = h.sink->lines.front();
    check_braced_list(r, grandchild, h.sink->lines);
    if (!utf8)
    {
        fuzz::check_shape(text, "\n");
        return 0;
    }
    ojson expected = ojson::object();
    expected["logger"] = "t";
    expected["level"] = "info";
    for (const auto &item : line.items)
    {
        expected[item.first] = item.second;
    }
    if (with_message)
    {
        expected["message"] = message;
    }
    const ojson parsed = fuzz::parse_line(text, "\n");
    FUZZ_CHECK(parsed.dump() == expected.dump());

    // Per-call fields do not stick: the next call on the same logger writes only the bound ones.
    h.sink->lines.clear();
    grandchild.info("bound only");
    ojson bound_only = ojson::object();
    bound_only["logger"] = "t";
    bound_only["level"] = "info";
    for (const auto &item : bound.items)
    {
        bound_only[item.first] = item.second;
    }
    bound_only["message"] = "bound only";
    FUZZ_CHECK(fuzz::parse_line(h.sink->last(), "\n").dump() == bound_only.dump());
    return 0;
}
