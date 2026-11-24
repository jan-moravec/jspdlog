#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a pointer other than a C string would silently convert to bool, so it is rejected.
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", static_cast<const char *>(nullptr)};
#else
    jspdlog::fields f{"v", static_cast<const int *>(nullptr)};
#endif
    return f.empty() ? 1 : 0;
}
