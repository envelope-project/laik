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
)

target_link_libraries ("laik"
    PRIVATE "${CMAKE_DL_LIBS}"
)

# Optional MPI backend
if (mpi-backend)
    find_pkgconfig ("mpi")
    find_program (mpirun "mpirun")

    if (TARGET "mpi" AND mpirun)
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
