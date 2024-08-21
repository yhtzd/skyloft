include(FetchContent)

# Fetch source code of rocksdb from github
FetchContent_Declare(
    rocksdb
    URL https://github.com/facebook/rocksdb/archive/refs/tags/v6.15.5.tar.gz
)
FetchContent_GetProperties(rocksdb)

if(NOT rocksdb_POPULATED)
    FetchContent_Populate(rocksdb)

    # Build rocksdb libraries automatically
    # add_subdirectory(${rocksdb_SOURCE_DIR} ${rocksdb_BINARY_DIR})
endif()

# Build rocksdb static library with custom command
add_custom_command(
    OUTPUT ${rocksdb_SOURCE_DIR}/librocksdb.a
    COMMAND ROCKSDB_DISABLE_ZSTD=1 ROCKSDB_DISABLE_BZIP=1 ROCKSDB_DISABLE_LZ4=1 make static_lib -j${ROCKSDB_JOBS}
    WORKING_DIRECTORY ${rocksdb_SOURCE_DIR}
    COMMENT "Build RocksDB static library"
    VERBATIM
)
add_custom_target(rocksdb DEPENDS ${rocksdb_SOURCE_DIR}/librocksdb.a)
add_library(librocksdb STATIC IMPORTED)
set_property(TARGET librocksdb PROPERTY IMPORTED_LOCATION ${rocksdb_SOURCE_DIR}/librocksdb.a)
set_target_properties(
    librocksdb PROPERTIES
    INTERFACE_INCLUDE_DIRECTORIES ${rocksdb_SOURCE_DIR}/include
)
add_dependencies(librocksdb rocksdb)
