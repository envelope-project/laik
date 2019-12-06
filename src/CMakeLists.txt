# target to re-create git-version.h on revision change
add_custom_target(git-version
    COMMAND "${CMAKE_SOURCE_DIR}/update-version.sh"
)
add_custom_command(
    OUTPUT "git-version.h"
    DEPENDS git-version
)

# Base library
add_library ("laik" SHARED
    "action.c"
    "backend.c"
    "core.c"
    "data.c"
    "debug.c"
    "external.c"
    "partitioner.c"
    "partitioning.c"
    "profiling.c"
    "program.c"
    "revinfo.c"
    "space.c"
    "slicearray.c"
    "type.c"
    "logging.c"
    "kvs.c"
    "fault-tolerance/checkpoint.c"
    "fault-tolerance/failure_detector.c")

SET_SOURCE_FILES_PROPERTIES(revinfo.c
    PROPERTIES OBJECT_DEPENDS
    ${CMAKE_CURRENT_BINARY_DIR}/git-version.h)

target_include_directories ("laik"
    PUBLIC "../include"
    PUBLIC "."
    PUBLIC "${CMAKE_CURRENT_BINARY_DIR}/."
)

target_link_libraries ("laik"
    PRIVATE "${CMAKE_DL_LIBS}"
)

# Optional MPI backend
if (mpi-backend)
    find_pkgconfig ("mpi" "${mpi-implementation}")

    # If pkg-config didn't find MPI above, also try CMake's native FindMPI
    # module since we really want to enable MPI support whenever possible!
    if (NOT TARGET "mpi")
        message(STATUS "Pkgconfig find mpi failed, trying fallback")
        find_package (MPI)
        if (MPI_C_FOUND)
            # MPI_C_COMPILE_FLAGS and MPI_C_LINK_FLAGS really ought to be be
            # semicolon-separated lists (the documentation [0] unfortunately
            # isn't quite clear in this point), but for at least CMake 3.5 - 3.9
            # these are space-separated strings instead (see e.g. [1]).
            #
            # While this bug was fixed with the rewrite of the FindMPI module in
            # CMake 3.10 [2], we unfortunately have to support the older CMake
            # versions too, so split the variables manually here.
            #
            # [0] https://cmake.org/cmake/help/v3.5/module/FindMPI.html
            # [1] https://github.com/Kitware/CMake/blob/v3.5.0/Modules/FindMPI.cmake#L325
            # [2] https://github.com/Kitware/CMake/commit/9a58e69cc8a9eea2e4f4ff0e65e7b1df909221ac
            if("${CMAKE_VERSION}" VERSION_LESS "3.10.0")
                separate_arguments (MPI_C_COMPILE_FLAGS)
                separate_arguments (MPI_C_LINK_FLAGS)
            endif ()

            add_library ("mpi" INTERFACE)
            target_include_directories ("mpi" INTERFACE "${MPI_C_INCLUDE_PATH}")
            target_compile_options     ("mpi" INTERFACE "${MPI_C_COMPILE_FLAGS}")
            target_link_libraries      ("mpi" INTERFACE "${MPI_C_LINK_FLAGS}")
            target_link_libraries      ("mpi" INTERFACE "${MPI_C_LIBRARIES}")
        endif ()
    endif ()

    if (TARGET "mpi")
        message (STATUS "Dependency check for option 'mpi-backend' succeeded, building!")

        target_sources ("laik"
            PRIVATE "backend-mpi.c"
        )

        target_compile_definitions ("laik"
            PRIVATE "USE_MPI"
        )

        target_link_libraries ("laik"
            PRIVATE "mpi"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'mpi-backend' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'mpi-backend' failed, stopping!")
    endif ()
endif ()

# Optional Single backend
if (single-backend)
    target_sources ("laik"
        PRIVATE "backend-single.c"
    )
endif ()

# Optional TCP backend
if (tcp-backend)
    find_pkgconfig ("gio"  "gio-2.0>=2.44")
    find_pkgconfig ("glib" "glib-2.0>=2.44")

    if (TARGET "gio" AND TARGET "glib")
        message (STATUS "Dependency check for option 'tcp-backend' succeeded, building!")

        target_sources ("laik"
            PRIVATE
                "backends/tcp/async.c"
                "backends/tcp/backend-tcp.c"
                "backends/tcp/client.c"
                "backends/tcp/condition.c"
                "backends/tcp/config.c"
                "backends/tcp/debug.c"
                "backends/tcp/errors.c"
                "backends/tcp/lock.c"
                "backends/tcp/map.c"
                "backends/tcp/messenger.c"
                "backends/tcp/minimpi.c"
                "backends/tcp/server.c"
                "backends/tcp/socket.c"
                "backends/tcp/socketpair.c"
                "backends/tcp/socketqueue.c"
                "backends/tcp/stats.c"
                "backends/tcp/task.c"
                "backends/tcp/time.c"
        )

        target_compile_definitions ("laik"
            PRIVATE "GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_44"
            PRIVATE "GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_44"
            PRIVATE "USE_TCP"
            # PRIVATE "LAIK_TCP_DEBUG"
            # PRIVATE "LAIK_TCP_STATS"
        )

        target_link_libraries ("laik"
            PRIVATE "gio"
            PRIVATE "glib"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'tcp-backend' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'tcp-backend' failed, stopping!")
    endif ()
endif ()

# Installation rules
install (DIRECTORY "../include/laik"            DESTINATION "include")
install (FILES     "../include/laik.h"          DESTINATION "include")
install (FILES     "../include/laik-internal.h" DESTINATION "include")
install (TARGETS   "laik"                       DESTINATION "lib")
