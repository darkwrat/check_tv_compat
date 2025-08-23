cmake_minimum_required(VERSION 3.10)
project(check_tv_compat C)

set(CMAKE_C_STANDARD 99)

# Add -ggdb3 to CMAKE_C_FLAGS
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -ggdb3")

find_package(PkgConfig REQUIRED)
pkg_check_modules(AVFORMAT REQUIRED IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC  REQUIRED IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL   REQUIRED IMPORTED_TARGET libavutil)

add_executable(check_tv_compat check_tv_compat.c)

target_include_directories(check_tv_compat PRIVATE
    ${AVFORMAT_INCLUDE_DIRS}
    ${AVCODEC_INCLUDE_DIRS}
    ${AVUTIL_INCLUDE_DIRS}
)

target_link_libraries(check_tv_compat
    PkgConfig::AVFORMAT
    PkgConfig::AVCODEC
    PkgConfig::AVUTIL
)

# Optional: show pkg-config info
message(STATUS "AVFORMAT libraries: ${AVFORMAT_LIBRARIES}")
message(STATUS "AVCODEC libraries: ${AVCODEC_LIBRARIES}")
message(STATUS "AVUTIL libraries: ${AVUTIL_LIBRARIES}")


install(TARGETS check_tv_compat
        RUNTIME DESTINATION bin)
