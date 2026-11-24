// Target 5: value types. A small tree built from the input is serialized through jspdlog and compared with an
// independent model. The node type has a serializer that uses the writer API and its leaves are standard types, so the
// writer and the dispatch are both covered.
#include "fuzz_support.h"

#include <array>
#include <chrono>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <string>
#include <tuple>
#include <variant>
#include <vector>

namespace
{

constexpr int MAX_DEPTH = 6;
constexpr int MAX_NODES = 64;

enum class unnamed : std::int16_t
{
    a = 1,
};

struct node
{
    enum class kind : std::uint8_t
    {
        object,
        array,
        leaf,
    };

    kind k = kind::leaf;
    std::vector<std::string> keys; // object only, unique
    std::vector<node> children;    // object values or array elements
    std::function<void(jspdlog::json_writer &)> write_leaf;
    fuzz::ojson model;
};

void jspdlog_serialize(jspdlog::json_writer &w, const node &n)
{
    switch (n.k)
    {
    case node::kind::object:
        w.begin_object();
        for (std::size_t i = 0; i < n.children.size(); ++i)
        {
            w.key(n.keys[i]);
            w.value(n.children[i]);
        }
        w.end_object();
        break;
    case node::kind::array:
        w.begin_array();
        for (const node &child : n.children)
        {
            w.value(child);
        }
        w.end_array();
        break;
    case node::kind::leaf:
        n.write_leaf(w);
        break;
    }
}

struct builder
{
    fuzz::reader &r;
    int nodes_left = MAX_NODES;
    bool all_utf8 = true;

    std::string text(std::size_t max_size)
    {
        std::string s = r.string(max_size);
        all_utf8 = all_utf8 && fuzz::is_valid_utf8(s);
        return s;
    }

    static fuzz::ojson number_or_null(double d) { return std::isfinite(d) ? fuzz::ojson(d) : fuzz::ojson(nullptr); }

    // Seconds to milliseconds with the saturation the library documents.
    static std::int64_t saturated_ms_of_seconds(std::int64_t s)
    {
        constexpr std::int64_t LIMIT = std::numeric_limits<std::int64_t>::max() / 1000;
        if (s > LIMIT)
        {
            return std::numeric_limits<std::int64_t>::max();
        }
        if (s < -LIMIT)
        {
            return std::numeric_limits<std::int64_t>::min();
        }
        return s * 1000;
    }

    node leaf()
    {
        node n;
        const auto kind = static_cast<unsigned>(r.byte()) % 12U;
        const auto set = [&n](const auto &value, fuzz::ojson model) {
            n.write_leaf = [value](jspdlog::json_writer &w) { w.value(value); };
            n.model = std::move(model);
        };
        switch (kind)
        {
        case 0:
        {
            const bool has = (r.byte() & 1U) != 0;
            const auto v = r.value<std::int32_t>();
            set(has ? std::optional<std::int32_t>{v} : std::nullopt, has ? fuzz::ojson(v) : fuzz::ojson(nullptr));
            break;
        }
        case 1:
        {
            using variant = std::variant<std::int64_t, std::string, bool>;
            switch (r.byte() % 3U)
            {
            case 0:
            {
                const auto v = r.value<std::int64_t>();
                set(variant{v}, fuzz::ojson(v));
                break;
            }
            case 1:
            {
                std::string s = text(8);
                set(variant{s}, fuzz::ojson(s));
                break;
            }
            default:
            {
                const bool v = (r.byte() & 1U) != 0;
                set(variant{v}, fuzz::ojson(v));
                break;
            }
            }
            break;
        }
        case 2:
        {
            std::vector<std::uint16_t> v;
            fuzz::ojson model = fuzz::ojson::array();
            for (unsigned i = r.byte() % 9U; i > 0; --i)
            {
                v.push_back(r.value<std::uint16_t>());
                model.push_back(v.back());
            }
            set(v, model);
            break;
        }
        case 3:
        {
            std::map<std::string, double> v;
            for (unsigned i = r.byte() % 4U; i > 0; --i)
            {
                const std::string key = text(4);
                v[key] = r.value<double>();
            }
            fuzz::ojson model = fuzz::ojson::object();
            for (const auto &entry : v)
            {
                model[entry.first] = number_or_null(entry.second);
            }
            set(v, model);
            break;
        }
        case 4:
        {
            std::array<std::int8_t, 3> v{};
            fuzz::ojson model = fuzz::ojson::array();
            for (auto &e : v)
            {
                e = r.value<std::int8_t>();
                model.push_back(e);
            }
            set(v, model);
            break;
        }
        case 5:
        {
            const bool b = (r.byte() & 1U) != 0;
            const std::string s = text(8);
            set(std::make_tuple(b, s, nullptr), fuzz::ojson::array({b, s, nullptr}));
            break;
        }
        case 6:
        {
            switch (r.byte() % 3U)
            {
            case 0:
            {
                const auto v = r.value<std::int64_t>();
                set(std::chrono::milliseconds(v), fuzz::ojson(v));
                break;
            }
            case 1:
            {
                const auto v = r.value<std::int64_t>();
                set(std::chrono::microseconds(v), fuzz::ojson(static_cast<double>(v) / 1000.0));
                break;
            }
            default:
            {
                const auto v = r.value<std::int64_t>();
                set(std::chrono::seconds(v), fuzz::ojson(saturated_ms_of_seconds(v)));
                break;
            }
            }
            break;
        }
        case 7:
        {
            const auto ms = r.value<std::int64_t>();
            const auto formatted = fuzz::rfc3339_utc_ms(ms);
            using point = std::chrono::time_point<std::chrono::system_clock, std::chrono::milliseconds>;
            set(point{std::chrono::milliseconds(ms)}, formatted ? fuzz::ojson(*formatted) : fuzz::ojson(nullptr));
            break;
        }
        case 8:
        {
            // An enum without a formatter is a number whatever {fmt} version is in use.
            const auto v = r.value<std::int16_t>();
            set(static_cast<unnamed>(v), fuzz::ojson(v));
            break;
        }
        case 9:
        {
            std::vector<bool> v;
            fuzz::ojson model = fuzz::ojson::array();
            for (unsigned i = r.byte() % 9U; i > 0; --i)
            {
                v.push_back((r.byte() & 1U) != 0);
                model.push_back(static_cast<bool>(v.back()));
            }
            set(v, model);
            break;
        }
        case 10:
        {
            std::map<int, std::string> v;
            for (unsigned i = r.byte() % 4U; i > 0; --i)
            {
                const auto key = r.value<std::int32_t>();
                v[key] = text(4);
            }
            fuzz::ojson model = fuzz::ojson::array();
            for (const auto &entry : v)
            {
                model.push_back(fuzz::ojson::array({entry.first, entry.second}));
            }
            set(v, model);
            break;
        }
        default:
        {
            const std::string key = fuzz::avoid_reserved_key(text(4), {"n", "flag"});
            const std::string value = text(6);
            const auto number = r.value<std::int8_t>();
            const bool flag = (r.byte() & 1U) != 0;
            jspdlog::fields f;
            f.insert(key, value);
            f.insert("n", number);
            f.insert("flag", flag);
            n.model = fuzz::ojson::object({{key, value}, {"n", number}, {"flag", flag}});
            n.write_leaf = [f](jspdlog::json_writer &w) { w.value(f); };
            break;
        }
        }
        n.k = node::kind::leaf;
        return n;
    }

    node build(int depth)
    {
        --nodes_left;
        const unsigned shape = r.byte() % 4U;
        if (depth >= MAX_DEPTH || nodes_left <= 0 || shape >= 2)
        {
            return leaf();
        }
        node n;
        const unsigned count = r.byte() % 5U;
        if (shape == 0)
        {
            n.k = node::kind::object;
            n.model = fuzz::ojson::object();
            for (unsigned i = 0; i < count && nodes_left > 0; ++i)
            {
                std::string key = text(6);
                if (n.model.contains(key))
                {
                    continue; // keys are unique
                }
                node child = build(depth + 1);
                n.model[key] = child.model;
                n.keys.push_back(std::move(key));
                n.children.push_back(std::move(child));
            }
        }
        else
        {
            n.k = node::kind::array;
            n.model = fuzz::ojson::array();
            for (unsigned i = 0; i < count && nodes_left > 0; ++i)
            {
                node child = build(depth + 1);
                n.model.push_back(child.model);
                n.children.push_back(std::move(child));
            }
        }
        return n;
    }
};

} // namespace

// NOLINTNEXTLINE(readability-identifier-naming): libFuzzer requires this name
extern "C" int LLVMFuzzerTestOneInput(const std::uint8_t *data, std::size_t size)
{
    fuzz::reader r(fuzz::as_text(data, size));
    builder b{r};
    const node root = b.build(0);

    const jspdlog::fields f{"v", root};
    const std::string json = f.to_json();
    fuzz::check_shape(json, "");

    const fuzz::harness h("t", fuzz::deterministic_options());
    h.logger.info(f);
    FUZZ_CHECK(h.sink->lines.size() == 1);
    const std::string line = h.sink->last();
    fuzz::check_shape(line, "\n");

    if (b.all_utf8)
    {
        const fuzz::ojson expected = fuzz::ojson::object({{"v", root.model}});
        FUZZ_CHECK(fuzz::ojson::parse(json) == expected);
        const fuzz::ojson parsed = fuzz::parse_line(line, "\n");
        FUZZ_CHECK(parsed["v"] == root.model);
    }
    return 0;
}
