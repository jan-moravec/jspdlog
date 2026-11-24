#include <jspdlog/jspdlog.h>

#include <chrono>
// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// only system_clock time points have a defined epoch.
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", std::chrono::system_clock::now()};
#else
    jspdlog::fields f{"v", std::chrono::steady_clock::now()};
#endif
    return f.empty() ? 1 : 0;
}
