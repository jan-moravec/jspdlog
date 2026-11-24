// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// the policy is set to the CMake spelling instead of a macro, which the preprocessor reads as 0. It must be rejected,
// not taken for the default policy.
#undef JSPDLOG_UTF8_POLICY
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
#define JSPDLOG_UTF8_POLICY JSPDLOG_UTF8_ESCAPE
#else
#define JSPDLOG_UTF8_POLICY escape
#endif

#include <jspdlog/jspdlog.h>

int main()
{
    const jspdlog::fields f{"v", 1};
    return f.empty() ? 1 : 0;
}
