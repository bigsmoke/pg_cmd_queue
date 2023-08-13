cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)

project(pg_cmd_queue LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 14)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

add_compile_options(-Wall)

add_executable(pg_cmd_queue_daemon
    optional.hpp
    argv_guard.h argv_guard.cpp
    envp_guard.h envp_guard.cpp
    utils.h utils.cpp
    lwpg_array.h lwpg_array.cpp
    lwpg_conn.h lwpg_conn.cpp
    lwpg_context.h lwpg_context.cpp
    lwpg_hstore.h lwpg_hstore.cpp
    lwpg_nullable.h lwpg_nullable.cpp
    lwpg_result.h lwpg_result.cpp
    lwpg_result_iterator.h lwpg_result_iterator.cpp
    lwpg_results.h lwpg_results.cpp
    cmdqueue.h cmdqueue.cpp
    cmdqueuerunner.h cmdqueuerunner.cpp
    nixqueuecmd.h nixqueuecmd.cpp
    nixqueuecmdcontext.h nixqueuecmdcontext.cpp
    pg_cmd_queue_daemon.cpp
)

target_include_directories(pg_cmd_queue_daemon PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(pg_cmd_queue_daemon pthread pq ${LIBPQ_LIBRARY_DIRS})
