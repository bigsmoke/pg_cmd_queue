cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)

project(pg_cmd_queue LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

add_compile_options(-Wall)

add_executable(pg_cmdqd
    utils.h utils.cpp
    logger.h logger.cpp
    pq-raii/libpq-raii.hpp
    pq_cmdqd_utils.h pq_cmdqd_utils.cpp
    fdguard.h fdguard.cpp
    pipefds.h pipefds.cpp
    cmdqueue.h cmdqueue.cpp
    cmdqueuerunner.h
    cmdqueuerunnermanager.h cmdqueuerunnermanager.cpp
    queuecmdmetadata.h queuecmdmetadata.cpp
    nixqueuecmd.h nixqueuecmd.cpp
    sqlqueuecmd.h sqlqueuecmd.cpp
    sigstate.h sigstate.cpp
    pg_cmd_queue_daemon.cpp
)

target_include_directories(pg_cmdqd PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(pg_cmdqd pthread ${LIBPQ_LINK_LIBRARIES})
link_directories(pg_cmdqd ${LIBPQ_LIBRARY_DIRS})

add_executable(nixtestcmd
    nixtestcmd.cpp
)

add_executable(pg_nix_queue_cmd
    utils.h utils.cpp
    logger.h logger.cpp
    pq-raii/libpq-raii.hpp
    pipefds.h pipefds.cpp
    cmdqueue.h cmdqueue.cpp
    queuecmdmetadata.h queuecmdmetadata.cpp
    nixqueuecmd.h nixqueuecmd.cpp
    pg_nix_queue_cmd.cpp
)

target_include_directories(pg_nix_queue_cmd PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(pg_nix_queue_cmd pthread ${LIBPQ_LINK_LIBRARIES})
link_directories(pg_nix_queue_cmd ${LIBPQ_LIBRARY_DIRS})

add_executable(with_cmdqd
    fdguard.h fdguard.cpp
    pipefds.h pipefds.cpp
    with_cmdqd.cpp
)

if (APPLE)
    set(APPLEISH TRUE)
endif()
if (APPLEISH OR DEFINED ENV{CMAKE_APPLEISH})
    add_definitions(-U_GNU_SOURCE)
endif()

add_definitions(-D_XOPEN_SOURCE=700)
