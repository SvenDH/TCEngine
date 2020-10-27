macro(libcoro_build)

    include (CheckFunctionExists)
    include (CheckIncludeFiles)
    include (CheckSymbolExists)

    add_library(coro ${PROJECT_SOURCE_DIR}/deps/libcoro/coro.c ${PROJECT_SOURCE_DIR}/deps/libcoro/coro.h)

    set(LIBCORO_INCLUDE_DIR ${PROJECT_SOURCE_DIR}/deps/libcoro)
    set(LIBCORO_LIBRARIES coro)

    check_include_files (ucontext.h HAVE_UCONTEXT_H)
    check_include_files (setjmp.h HAVE_SETJMP_H)
    check_symbol_exists (sigaltstack "signal.h" HAVE_SIGALTSTACK)

    if(WIN32)
        set(CORO_FIBER ON)
    elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "86" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "amd64")
        set(CORO_ASM ON)
    elseif (${CMAKE_SYSTEM_PROCESSOR} MATCHES "arm" OR ${CMAKE_SYSTEM_PROCESSOR} MATCHES "aarch64")
        set(CORO_ASM ON)
    else()
        set(CORO_SJLJ ON)
    endif()

    configure_file(${PROJECT_SOURCE_DIR}/deps/libcoro/config.h.in ${PROJECT_SOURCE_DIR}/deps/libcoro/config.h)

    unset(coro_src)
endmacro(libcoro_build)