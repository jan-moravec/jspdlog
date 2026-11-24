#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// wide characters are rejected: convert to a UTF-8 std::string.
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", 'x'};
#else
    jspdlog::fields f{"v", L'x'};
#endif
    return f.empty() ? 1 : 0;
}
