# Strict warnings for jspdlog's own tests and examples. Users compile jspdlog.h with their own warning flags, so the
# header must stay clean under a strict set. Never applied to the jspdlog::jspdlog target itself.
#
# Adapted from https://github.com/cpp-best-practices/cmake_template (cmake/CompilerWarnings.cmake).

function(jspdlog_set_warnings target)
    set(msvc_warnings
        /W4
        /w14242 # conversion, possible loss of data
        /w14254 # bit-field conversion, possible loss of data
        /w14263 # member function does not override a base class virtual function
        /w14265 # class has virtual functions but a non-virtual destructor
        /w14287 # unsigned/negative constant mismatch
        /we4289 # loop control variable used outside the for-loop scope
        /w14296 # expression is always true/false
        /w14311 # pointer truncation
        /w14545 # expression before comma evaluates to a function missing an argument list
        /w14546 # function call before comma missing argument list
        /w14547 # operator before comma has no effect
        /w14549 # operator before comma has no effect
        /w14555 # expression has no effect
        /w14619 # pragma warning: there is no warning number
        /w14640 # thread-unsafe static member initialization
        /w14826 # sign-extended conversion
        /w14905 # wide string literal cast to LPSTR
        /w14906 # string literal cast to LPWSTR
        /w14928 # more than one user-defined conversion applied implicitly
        /permissive-
        /utf-8
    )

    set(clang_warnings
        -Wall
        -Wextra
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wnull-dereference
        -Wdouble-promotion
        -Wformat=2
        -Wimplicit-fallthrough
    )

    set(gcc_warnings
        ${clang_warnings}
        -Wmisleading-indentation
        -Wduplicated-cond
        -Wduplicated-branches
        -Wlogical-op
        -Wuseless-cast
    )

    if(JSPDLOG_WARNINGS_AS_ERRORS)
        list(APPEND msvc_warnings /WX)
        list(APPEND clang_warnings -Werror)
        list(APPEND gcc_warnings -Werror)
    endif()

    if(MSVC)
        # Also covers clang-cl, which accepts the MSVC flags.
        set(warnings ${msvc_warnings})
    elseif(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
        set(warnings ${clang_warnings})
    elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
        set(warnings ${gcc_warnings})
    else()
        message(WARNING "jspdlog: no warning set for ${CMAKE_CXX_COMPILER_ID}")
    endif()

    target_compile_options(${target} PRIVATE ${warnings})
endfunction()
