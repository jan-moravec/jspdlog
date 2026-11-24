#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a type without a JSON form asks for jspdlog_serialize or a serializer.
struct unknown
{
};

struct known
{
};

inline void jspdlog_serialize(jspdlog::json_writer &w, const known &)
{
    w.value(1);
}

int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", known{}};
#else
    jspdlog::fields f{"v", unknown{}};
#endif
    return f.empty() ? 1 : 0;
}
