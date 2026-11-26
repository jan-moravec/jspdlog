#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a key with a quote must not compile, because it is checked at compile time (C++20).
int main()
{
    using namespace jspdlog::literals;
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    const jspdlog::key k = "ab"_key;
#else
    const jspdlog::key k = "a\"b"_key;
#endif
    return k.view().empty() ? 1 : 0;
}
