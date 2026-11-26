#include <jspdlog/jspdlog.h>

// Built with JSPDLOG_COMPILE_FAIL_CONTROL, the valid line must compile. Without it:
// a writable char array is not a key, because its contents can change after the check.
int main()
{
#ifdef JSPDLOG_COMPILE_FAIL_CONTROL
    static constexpr char text[] = "ab";
    constexpr jspdlog::key k{text};
#else
    char text[] = "ab";
    const jspdlog::key k{text};
#endif
    return k.view().empty() ? 1 : 0;
}
