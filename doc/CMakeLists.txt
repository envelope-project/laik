if (documentation)
    find_program (pandoc "pandoc")

    if (pandoc)
        message (STATUS "Dependency check for option 'documentation' succeeded, building!")
        set (docs
            "index.md"
            "Debugging.md"
            "TODO.md"
        )

        foreach (doc ${docs})
            get_filename_component (name "${doc}" NAME_WE)

            add_pandoc_target ("${name}.html" "${doc}")
            add_pandoc_target ("${name}.pdf"  "${doc}")
        endforeach ()

        add_pandoc_target ("all.html" ${docs})
        add_pandoc_target ("all.pdf"  ${docs})
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'documentation' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'documentation' failed, stopping!")
    endif ()
endif ()
