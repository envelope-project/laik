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
        message (WARNING "Dependency check for option 'mpi-backend' failed, skipping!")
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
