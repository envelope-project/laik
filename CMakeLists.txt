# Set the project name
project ("laik")

# Set the minimal CMake version required
cmake_minimum_required (VERSION 3.5)

# Enable testing with CMake's CTest
enable_testing ()

# Set some global defaults
add_compile_options (
    "-Wall"
    "-Werror"
    "-Wextra"
    "-Wpedantic"
)
set (CMAKE_C_STANDARD            11)
set (CMAKE_C_EXTENSIONS          ON)
set (CMAKE_C_STANDARD_REQUIRED   ON)
set (CMAKE_CXX_STANDARD          11)
set (CMAKE_CXX_EXTENSIONS        ON)
set (CMAKE_CXX_STANDARD_REQUIRED ON)

# Declare the options
option (enable-cpp-examples      "Enable C++ examples (needs a C++ compiler)"                                  ON)
option (enable-documentation     "Enable documentation (needs pandoc and texlive)"                             ON)
option (enable-failure-simulator "Enable failure simulator (needs libprotobuf-c-dev and protobuf-c-compiler)"  ON)
option (enable-mosquitto-agent   "Enable Mosquitto agent (needs libmosquitto-dev)"                             ON)
option (enable-mpi-backend       "Enable MPI backend (needs libopenmpi-dev, openmpi-bin, and openssh-client)"  ON)
option (enable-openmp-examples   "Enable OpenMP examples (needs an OpenMP-capable compiler)"                   ON)
option (enable-profiling-agent   "Enable profiling agent (needs libpapi-dev)"                                  ON)
option (enable-simple-agent      "Enable simple agent"                                                         ON)
option (enable-single-backend    "Enable Single backend"                                                       ON)

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
endfunction (add_pandoc_target)

# Add a pkg-config helper function
function (add_pkgconfig_library name)
    if (NOT TARGET "${name}")
        find_package (PkgConfig REQUIRED)
        pkg_check_modules ("${name}" REQUIRED "${name}")
        add_library ("${name}" INTERFACE)
        target_compile_options ("${name}" INTERFACE "${${name}_CFLAGS}")
        target_link_libraries  ("${name}" INTERFACE "${${name}_LDFLAGS}")
    endif (NOT TARGET "${name}")
endfunction (add_pkgconfig_library)

# Add a protobuf helper function
function (compile_protobuf_module name)
    add_custom_command (
        COMMAND           "protoc-c" "--c_out" "${CMAKE_CURRENT_BINARY_DIR}" "${name}.proto"
        DEPENDS           "${name}.proto"
        OUTPUT            "${name}.pb-c.c" "${name}.pb-c.h"
        VERBATIM
        WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
    )
endfunction (compile_protobuf_module)

# Add the subdirectories
add_subdirectory ("doc")
add_subdirectory ("examples")
add_subdirectory ("external/FT-STIM")
add_subdirectory ("external/MQTT")
add_subdirectory ("external/profiling")
add_subdirectory ("external/simple")
add_subdirectory ("src")
add_subdirectory ("tests")
