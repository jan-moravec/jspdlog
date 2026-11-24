#include <jspdlog/jspdlog.h>

#include <filesystem>
// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// std::filesystem::path is a range of paths, so it is rejected: convert it with string().
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    jspdlog::fields f{"v", std::filesystem::path{"a"}.string()};
#else
    jspdlog::fields f{"v", std::filesystem::path{"a"}};
#endif
    return f.empty() ? 1 : 0;
}
