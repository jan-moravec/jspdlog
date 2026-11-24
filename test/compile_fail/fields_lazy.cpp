#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a stored fields object is serialized once and cannot hold a lazy value.
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    const jspdlog::fields f{"a", 1};
#else
    const jspdlog::fields f{"a", [] { return 1; }};
#endif
    return f.empty() ? 1 : 0;
}
