cmake_minimum_required(VERSION 3.5)
project(skyloft LANGUAGES C CXX ASM)

if(DEBUG)
    set(CMAKE_BUILD_TYPE "Debug")
    add_definitions(-DDEBUG)
endif()

set(CMAKE_C_STANDARD 23)
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -O3 -Wall -Wextra -Wno-unused-parameter")
if (DPDK)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS}")
elseif (SCHED_POLICY MATCHES "^(fifo|rr|cfs|eevdf)$")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -mno-sse")
endif()

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -W -Wextra -Wall")
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wsign-compare -Wshadow -Wno-unused-parameter -Wno-unused-variable -Woverloaded-virtual -Wnon-virtual-dtor -Wno-missing-field-initializers -Wno-strict-aliasing")

set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} -T ${CMAKE_CURRENT_SOURCE_DIR}/libos/libos.ld")

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-omit-frame-pointer")

    include(CheckCXXCompilerFlag)
    CHECK_CXX_COMPILER_FLAG("-momit-leaf-frame-pointer" HAVE_OMIT_LEAF_FRAME_POINTER)

    if(HAVE_OMIT_LEAF_FRAME_POINTER)
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -momit-leaf-frame-pointer")
    endif()
endif()

if(NOT USE_RTTI)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fno-rtti")
endif()

# Hook exit in all targets
add_compile_options(-Wl,--wrap=exit)
add_link_options(-Wl,--wrap=exit)

set(CMAKE_SCRIPTS ${CMAKE_CURRENT_SOURCE_DIR}/scripts/cmake/)

if(STAT)
    add_definitions(-DSKYLOFT_STAT)
endif()

if(UINTR)
    add_definitions(-DSKYLOFT_UINTR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -muintr")
endif()

if(FXSAVE)
    add_definitions(-DSKYLOFT_FXSAVE)
endif()

if(LOG_LEVEL)
    if(NOT LOG_LEVEL MATCHES "^(debug|info|notice|warn|err|crit)$")
        message(FATAL_ERROR "Invalid log level: ${LOG_LEVEL}")
    endif()

    string(TOUPPER ${LOG_LEVEL} LOG_LEVEL_UPPER)
    add_definitions(-DLOG_LEVEL_${LOG_LEVEL_UPPER})
endif()
message(STATUS "Log level: ${LOG_LEVEL}")

if(NOT CMAKE_BUILD_TYPE OR CMAKE_BUILD_TYPE STREQUAL "")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "" FORCE)
endif()

message(STATUS "Build type: ${CMAKE_BUILD_TYPE}")
message(STATUS "Schedule policy: ${SCHED_POLICY}")

if(SCHED_POLICY STREQUAL "fifo")
    add_definitions(-DSKYLOFT_SCHED_FIFO)
elseif(SCHED_POLICY STREQUAL "rr")
    add_definitions(-DSKYLOFT_SCHED_FIFO2)
elseif(SCHED_POLICY STREQUAL "cfs")
    add_definitions(-DSKYLOFT_SCHED_CFS)
elseif(SCHED_POLICY STREQUAL "eevdf")
    add_definitions(-DSKYLOFT_SCHED_EEVDF)
elseif(SCHED_POLICY STREQUAL "sq")
    add_definitions(-DSKYLOFT_SCHED_SQ)
elseif(SCHED_POLICY STREQUAL "sq_lcbe")
    add_definitions(-DSKYLOFT_SCHED_SQ_LCBE)
endif()

add_subdirectory(utils)
add_subdirectory(libos)

if (SCHED_POLICY MATCHES "^(sq|sq_lcbe)$")
    add_subdirectory(synthetic)
else()
    add_subdirectory(apps)
endif()

add_custom_target(
    microbench
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/microbench
    COMMAND make clean && make SKYLOFT_DIR=${CMAKE_CURRENT_BINARY_DIR}/install
    COMMAND ln -sf ${CMAKE_CURRENT_SOURCE_DIR}/microbench/thread ${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/thread
)
