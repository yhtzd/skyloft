add_definitions(-D_GNU_SOURCE)

aux_source_directory(. UTILS_SRCS)
add_library(utils ${UTILS_SRCS})
target_include_directories(utils PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
