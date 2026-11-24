#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a class that only converts to bool is not a bool; cast it or give it a serializer.
struct flag
{
    // Implicit on purpose: this is the conversion that no longer counts as a bool.
    // NOLINTNEXTLINE(google-explicit-constructor,hicpp-explicit-conversions)
    operator bool() const { return true; }
};

int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", static_cast<bool>(flag{})};
#else
    jspdlog::fields f{"v", flag{}};
#endif
    return f.empty() ? 1 : 0;
}
