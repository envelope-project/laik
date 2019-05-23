# Set the project name
project ("laik")

# Set the minimal CMake version required
cmake_minimum_required (VERSION 3.5)

# Enable testing with CMake's CTest
enable_testing ()

# Set some global defaults
add_compile_options (
    "-pedantic"
    "-UNDEBUG" # Work around https://github.com/envelope-project/laik/issues/92
    "-Wall"
    "-Werror"
    "-Wextra"
)
set (CMAKE_C_STANDARD            11)
set (CMAKE_C_EXTENSIONS          ON)
set (CMAKE_C_STANDARD_REQUIRED   ON)
set (CMAKE_CXX_STANDARD          11)
set (CMAKE_CXX_EXTENSIONS        ON)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Declare the options
option (cpp-examples      "Enable C++ examples (needs a C++ compiler)"                                  on)
option (documentation     "Enable documentation (needs pandoc and texlive)"                             on)
option (failure-simulator "Enable failure simulator (needs libprotobuf-c-dev and protobuf-c-compiler)"  on)
option (mosquitto-agent   "Enable Mosquitto agent (needs libmosquitto-dev)"                             on)
option (mpi-backend       "Enable MPI backend (needs libopenmpi-dev, openmpi-bin, and openssh-client)"  on)
option (openmp-examples   "Enable OpenMP examples (needs an OpenMP-capable compiler)"                   on)
option (profiling-agent   "Enable profiling agent (needs libpapi-dev)"                                  on)
option (simple-agent      "Enable simple agent"                                                         on)
option (single-backend    "Enable Single backend"                                                       on)
option (skip-missing      "Enable skipping optional features in the case of missing dependencies"       on)
option (tcp-backend       "Enable TCP backend (needs libglib2.0-dev)"                                   on)

set (mpi-implementation "mpi" CACHE STRING "The pkg-config name of the MPI implementation to use (defaults to 'mpi')")

# Add a pandoc helper function
function (add_pandoc_target target)
    add_custom_command (
        COMMAND           "pandoc" "--output" "${CMAKE_CURRENT_BINARY_DIR}/${target}" "--" ${ARGN}
        DEPENDS           "${ARGN}"
        OUTPUT            "${target}"
        VERBATIM
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    add_custom_target ("${target}_target" ALL DEPENDS "${target}")
endfunction ()

# Add a protobuf helper function
function (compile_protobuf_module name)
    add_custom_command (
        COMMAND           "protoc-c" "--c_out" "${CMAKE_CURRENT_BINARY_DIR}" "${name}.proto"
        DEPENDS           "${name}.proto"
        OUTPUT            "${name}.pb-c.c" "${name}.pb-c.h"
        VERBATIM
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endfunction ()

# Add a pkg-config helper function
function (find_pkgconfig target name)
    if (NOT TARGET "${target}")
        find_package (PkgConfig)
        if (PkgConfig_FOUND)
            pkg_check_modules ("${target}" "${name}")
            if (${target}_FOUND)
                add_library ("${target}" INTERFACE)
                target_compile_options ("${target}" INTERFACE "${${target}_CFLAGS}")
                target_link_libraries  ("${target}" INTERFACE "${${target}_LDFLAGS}")
            endif ()
        endif ()
    endif ()
endfunction ()

# Add the subdirectories
add_subdirectory ("doc")
add_subdirectory ("examples")
add_subdirectory ("external/FT-STIM")
add_subdirectory ("external/MQTT")
add_subdirectory ("external/profiling")
add_subdirectory ("external/simple")
add_subdirectory ("src")
add_subdirectory ("tests")
add_subdirectory ("tests/mpi")
add_subdirectory ("tests/tcp")
add_subdirectory ("tests/mpi/unit_tests/")
