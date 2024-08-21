enable_language(C ASM)

configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../params.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/skyloft/uapi/params.h
    COPYONLY
)
configure_file(
    ${CMAKE_CURRENT_SOURCE_DIR}/../params.h.in
    ${CMAKE_CURRENT_SOURCE_DIR}/../include/skyloft/params.h
    COPYONLY
)

aux_source_directory(sched SCHED_SRCS)
aux_source_directory(platform PLATFORM_SRCS)
aux_source_directory(mm MM_SRCS)
aux_source_directory(net NET_SRCS)
aux_source_directory(shim SHIM_SRCS)
aux_source_directory(sync SYNC_SRCS)
aux_source_directory(io IO_SRCS)
set(TOP_SRCS libos.c percpu.c exit.c stat.c)

add_definitions(-D_GNU_SOURCE)

if(SIGNAL)
    add_definitions(-DSKYLOFT_SIGNAL_SWITCH)
endif()

if(TIMER)
    add_definitions(-DSKYLOFT_TIMER)
endif()

if(UINTR)
    add_definitions(-DSKYLOFT_TIMER)
    add_definitions(-DSKYLOFT_UINTR)
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -muintr")
endif()

if(SCHED_POLICY)
    set(SCHED_POLICY_SRC sched/policy/${SCHED_POLICY}.c)
endif()

set(SCHED_SRCS ${SCHED_SRCS} ${SCHED_POLICY_SRC} sched/switch.S)
set(ALL_SRCS ${TOP_SRCS} ${SCHED_SRCS} ${PLATFORM_SRCS} ${MM_SRCS} ${SYNC_SRCS} ${NET_SRCS})

if(DPDK)
    set(ALL_SRCS ${ALL_SRCS} ${IO_SRCS})
endif()

add_library(skyloft ${ALL_SRCS})
target_link_libraries(skyloft utils pthread rt numa)
target_include_directories(skyloft PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/../include)

if(DPDK)
    find_package(PkgConfig REQUIRED)
    pkg_check_modules(LIBDPDK REQUIRED libdpdk)
    target_compile_options(skyloft PRIVATE ${LIBDPDK_CFLAGS})
    target_link_libraries(skyloft ${LIBDPDK_LDFLAGS})
    add_definitions(-DSKYLOFT_DPDK)
endif()

add_library(shim ${SHIM_SRCS})
target_link_libraries(shim dl utils skyloft)

# Install static libraries, linker scripts and public headers
install(TARGETS skyloft utils shim ARCHIVE DESTINATION lib LIBRARY DESTINATION lib)
install(FILES libos.ld DESTINATION lib)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../include/ DESTINATION include)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../utils/include/ DESTINATION include)
