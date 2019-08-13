# Build the C examples
foreach (test
        "checkpoint-osu-latency"
        "checkpoint-osu-latency-ring"
        )

    add_executable ("${test}"
            "${test}.c"
            "osu_util.h"
            "osu_util.c"
            "osu_util_mpi.h"
            "osu_util_mpi.c"
            )

#    get_source_file_property( MYPROPS osu_util.c COMPILE_FLAGS )
#    STRING( REPLACE "-Werror" " " MYPROPS ${MYPROPS} )
#    set_source_files_properties( osu_util.c COMPILE_FLAGS ${MYPROPS} )
#
#    get_source_file_property( MYPROPS osu_util_mpi.c COMPILE_FLAGS )
#    STRING( REPLACE "-Werror" " " MYPROPS ${MYPROPS} )
#    set_source_files_properties( osu_util_mpi.c COMPILE_FLAGS ${MYPROPS} )


    set_source_files_properties( osu_util.c PROPERTIES COMPILE_FLAGS "-Wno-error=implicit-fallthrough -Wno-error=unused-parameter -Wno-error=unused-but-set-parameter" )
    set_source_files_properties( osu_util_mpi.c PROPERTIES COMPILE_FLAGS "-Wno-error=implicit-fallthrough -Wno-error=unused-parameter -Wno-error=unused-but-set-parameter" )

    target_compile_options("${test}"
            PRIVATE
            "-Wall"
            "-Wextra"
            )

    target_link_libraries ("${test}"
            PRIVATE "laik"
            PRIVATE "m"
            PRIVATE "mpi"
            )
endforeach ()