// Includes jspdlog/nlohmann.h and nothing else, which checks that the header compiles on its own. The file is named
// after the header so that clang-tidy, linting the header, takes the compile command of this unit, with nlohmann/json
// on its include path.

#include <jspdlog/nlohmann.h>
