cmake_minimum_required(VERSION 3.5)
cmake_policy(SET CMP0048 NEW)

project(pg_cmd_queue LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)

find_package(PkgConfig REQUIRED)
pkg_check_modules(LIBPQ REQUIRED IMPORTED_TARGET libpq)

add_compile_options(-Wall)

add_executable(pg_cmd_queue_daemon
    utils.h utils.cpp
    logger.h logger.cpp
    lwpg_array.h lwpg_array.cpp
    lwpg_conn.h lwpg_conn.cpp
    lwpg_context.h lwpg_context.cpp
    lwpg_error.h lwpg_error.cpp
    lwpg_hstore.h lwpg_hstore.cpp
    lwpg_nullable.h lwpg_nullable.cpp
    lwpg_result.h lwpg_result.cpp
    lwpg_result_iterator.h lwpg_result_iterator.cpp
    lwpg_results.h lwpg_results.cpp
    lwpg_string.h lwpg_string.cpp
    fdguard.h fdguard.cpp
    pipefds.h pipefds.cpp
    cmdqueue.h cmdqueue.cpp
    cmdqueuerunner.h
    queuecmdmetadata.h queuecmdmetadata.cpp
    nixqueuecmd.h nixqueuecmd.cpp
    sqlqueuecmd.h sqlqueuecmd.cpp
    pg_cmd_queue_daemon.cpp
)

target_include_directories(pg_cmd_queue_daemon PUBLIC ${LIBPQ_INCLUDE_DIRS})
target_link_libraries(pg_cmd_queue_daemon pthread ${LIBPQ_LINK_LIBRARIES})
link_directories(pg_cmd_queue_daemon ${LIBPQ_LIBRARY_DIRS})
