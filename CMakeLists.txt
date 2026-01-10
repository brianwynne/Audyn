cmake_minimum_required(VERSION 3.16)
project(audyn
    VERSION 1.0.0
    DESCRIPTION "AES67 Audio Capture & Archival Engine"
    LANGUAGES C
)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Build type
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release)
endif()

# Compiler flags
set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -Wall -Wextra")
set(CMAKE_C_FLAGS_DEBUG "-g -O0 -DDEBUG")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")

# Find required packages
find_package(PkgConfig REQUIRED)
pkg_check_modules(PIPEWIRE REQUIRED libpipewire-0.3)
pkg_check_modules(OPUS REQUIRED opus)
pkg_check_modules(OGG REQUIRED ogg)

# Find pthreads
find_package(Threads REQUIRED)

# Source files
set(AUDYN_SOURCES
    audyn.c
    core/log.c
    core/frame_pool.c
    core/audio_queue.c
    sink/wav_sink.c
    sink/opus_sink.c
    input/pipewire_input.c
    input/aes_input.c
)

# Header files (for IDE support)
set(AUDYN_HEADERS
    core/log.h
    core/frame_pool.h
    core/audio_queue.h
    sink/wav_sink.h
    sink/opus_sink.h
    input/pipewire_input.h
    input/aes_input.h
)

# Main executable
add_executable(audyn ${AUDYN_SOURCES} ${AUDYN_HEADERS})

target_include_directories(audyn PRIVATE
    ${CMAKE_SOURCE_DIR}/core
    ${CMAKE_SOURCE_DIR}/input
    ${CMAKE_SOURCE_DIR}/sink
    ${PIPEWIRE_INCLUDE_DIRS}
    ${OPUS_INCLUDE_DIRS}
    ${OGG_INCLUDE_DIRS}
)

target_link_libraries(audyn PRIVATE
    ${PIPEWIRE_LIBRARIES}
    ${OPUS_LIBRARIES}
    ${OGG_LIBRARIES}
    Threads::Threads
)

target_compile_options(audyn PRIVATE
    ${PIPEWIRE_CFLAGS_OTHER}
    ${OPUS_CFLAGS_OTHER}
    ${OGG_CFLAGS_OTHER}
)

# Installation
include(GNUInstallDirs)

install(TARGETS audyn
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Optional: Install systemd service file
if(EXISTS "${CMAKE_SOURCE_DIR}/packaging/audyn.service")
    install(FILES packaging/audyn.service
        DESTINATION ${CMAKE_INSTALL_PREFIX}/lib/systemd/system
        OPTIONAL
    )
endif()

# Optional: Install config file
if(EXISTS "${CMAKE_SOURCE_DIR}/audyn.conf")
    install(FILES audyn.conf
        DESTINATION ${CMAKE_INSTALL_SYSCONFDIR}/audyn
        OPTIONAL
    )
endif()

# Package information
set(CPACK_PACKAGE_NAME "audyn")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_PACKAGE_CONTACT "B. Wynne")
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libpipewire-0.3-0, libopus0, libogg0")
include(CPack)

# Print configuration summary
message(STATUS "")
message(STATUS "Audyn configuration:")
message(STATUS "  Version:      ${PROJECT_VERSION}")
message(STATUS "  Build type:   ${CMAKE_BUILD_TYPE}")
message(STATUS "  C Compiler:   ${CMAKE_C_COMPILER}")
message(STATUS "  Install to:   ${CMAKE_INSTALL_PREFIX}")
message(STATUS "")
