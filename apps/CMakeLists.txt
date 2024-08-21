add_executable(hello hello.c)
target_link_libraries(hello skyloft)

add_executable(bench bench.c)
target_link_libraries(bench skyloft)

add_executable(bench_app_switch bench_app_switch.c)
target_link_libraries(bench_app_switch skyloft)

add_executable(bench_pthread bench_pthread.c)
target_link_libraries(bench_pthread pthread utils)

add_executable(hello_shim hello_shim.c)
target_link_libraries(hello_shim PRIVATE shim)
set_target_properties(hello_shim PROPERTIES LINK_FLAGS "-Wl,--wrap=main")
target_include_directories(hello_shim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../shim/include)

add_executable(test_timer test_timer.c)
target_link_libraries(test_timer skyloft utils)

add_executable(test_rcu test_rcu.c)
target_link_libraries(test_rcu skyloft utils)

add_executable(test_net test_net.c)
target_link_libraries(test_net skyloft utils)

if(DPDK)
    add_executable(fakework fakework.c)
    target_link_libraries(fakework skyloft utils)

    include(${CMAKE_SCRIPTS}/rocksdb.mk)
    add_custom_target(
        rocksdb_server
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb_server
        COMMAND make clean && make SKYLOFT_DIR=${CMAKE_CURRENT_BINARY_DIR}/../install ROCKSDB_SRC=${rocksdb_SOURCE_DIR}/
        COMMAND ln -sf ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb_server/rocksdb_server ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/rocksdb_server
    )
    add_dependencies(rocksdb_server librocksdb)
endif()

add_custom_target(
    memcached
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/memcached
    COMMAND ./autogen.sh
    COMMAND ./configure --with-skyloft=${CMAKE_CURRENT_BINARY_DIR}/../install
    COMMAND make clean && make -j
    COMMAND cp memcached ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/memcached
)

add_custom_target(
    schbench
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/schbench
    COMMAND make clean && make SKYLOFT_DIR=${CMAKE_CURRENT_BINARY_DIR}/../install
    COMMAND cp schbench ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/schbench
)
