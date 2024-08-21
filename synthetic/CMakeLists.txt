include(${CMAKE_SCRIPTS}/rocksdb.mk)

set(COMMON_SRCS rocksdb/random.cc rocksdb/common.cc)

if (UINTR)
    if (SCHED_POLICY STREQUAL "sq")
        add_executable(shinjuku rocksdb/shinjuku.cc ${COMMON_SRCS})
        target_link_libraries(shinjuku PRIVATE
            skyloft
            librocksdb
            z
            gflags
            snappy
        )
        target_include_directories(shinjuku PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb)
    elseif (SCHED_POLICY STREQUAL "sq_lcbe")
        add_executable(shinjuku rocksdb/shinjuku.cc ${COMMON_SRCS})
        target_link_libraries(shinjuku PRIVATE
            skyloft
            librocksdb
            z
            gflags
            snappy
        )
        target_include_directories(shinjuku PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb)

        set(ANTAGONIST_SOURCES antagonist/main.cc)
        add_executable(antagonist ${ANTAGONIST_SOURCES})
        target_link_libraries(antagonist PRIVATE skyloft gflags)
        target_include_directories(antagonist PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/rocksdb)
    endif()
endif()
