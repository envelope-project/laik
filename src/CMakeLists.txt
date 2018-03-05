# Base library
add_library ("laik" SHARED
    "backend.c"
    "core.c"
    "data.c"
    "debug.c"
    "external.c"
    "partitioner.c"
    "partitioning.c"
    "profiling.c"
    "program.c"
    "space.c"
    "type.c"
)

target_include_directories ("laik"
    PUBLIC "../include"
    PUBLIC "."
)

target_link_libraries ("laik"
    PRIVATE "${CMAKE_DL_LIBS}"
)

# Optional MPI backend
if (mpi-backend)
    find_pkgconfig ("mpi")

    # If pkg-config didn't find MPI above, also try CMake's native FindMPI
    # module since we really want to enable MPI support whenever possible!
    if (NOT TARGET "mpi")
        find_package (MPI)
        if (MPI_C_FOUND)
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
    find_pkgconfig ("gio-2.0")
    find_pkgconfig ("glib-2.0")

    if (TARGET "gio-2.0" AND TARGET "glib-2.0")
        message (STATUS "Dependency check for option 'tcp-backend' succeeded, building!")

        target_sources ("laik"
            PRIVATE
                "backends/tcp/backend.c"
                "backends/tcp/client.c"
                "backends/tcp/config.c"
                "backends/tcp/debug.c"
                "backends/tcp/errors.c"
                "backends/tcp/map.c"
                "backends/tcp/messenger.c"
                "backends/tcp/minimpi.c"
                "backends/tcp/server.c"
                "backends/tcp/socket.c"
                "backends/tcp/task.c"
        )

        target_compile_definitions ("laik"
            PRIVATE "GLIB_VERSION_MAX_ALLOWED=GLIB_VERSION_2_48"
            PRIVATE "GLIB_VERSION_MIN_REQUIRED=GLIB_VERSION_2_48"
            PRIVATE "LAIK_TCP_BACKEND_AVAILABLE"
            # PRIVATE "LAIK_TCP_DEBUG"
        )

        target_link_libraries ("laik"
            PRIVATE "gio-2.0"
            PRIVATE "glib-2.0"
        )
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'tcp-backend' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'tcp-backend' failed, stopping!")
    endif ()
endif ()

# Installation rules
install (TARGETS "laik" DESTINATION "lib")

install (FILES "../include/laik.h"                DESTINATION "include")
install (FILES "../include/laik-backend-mpi.h"    DESTINATION "include")
install (FILES "../include/laik-backend-single.h" DESTINATION "include")
install (FILES "../include/interface/agent.h"     DESTINATION "include/interface")
install (FILES "../include/laik/backend.h"        DESTINATION "include/laik")
install (FILES "../include/laik/core.h"           DESTINATION "include/laik")
install (FILES "../include/laik/data.h"           DESTINATION "include/laik")
install (FILES "../include/laik/debug.h"          DESTINATION "include/laik")
install (FILES "../include/laik/definitions.h"    DESTINATION "include/laik")
install (FILES "../include/laik/ext.h"            DESTINATION "include/laik")
install (FILES "../include/laik/profiling.h"      DESTINATION "include/laik")
install (FILES "../include/laik/program.h"        DESTINATION "include/laik")
install (FILES "../include/laik/space.h"          DESTINATION "include/laik")
