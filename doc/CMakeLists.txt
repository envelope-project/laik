if (documentation)
    find_program (pandoc "pandoc")
    find_program (pandoc_pdf "pdflatex")

    if (pandoc)
        message (STATUS "Dependency check for option 'documentation' succeeded, building!")
        if (pandoc_pdf)
            message (STATUS "  Creating PDF doc (pdflatex found)")
        else ()
            message (STATUS "  Skip PDF doc creation (pdflatex not found)")
        endif ()

        set (docs
            "index.md"
            "Debugging.md"
            "TODO.md"
        )

        foreach (doc ${docs})
            get_filename_component (name "${doc}" NAME_WE)

            add_pandoc_target ("${name}.html" "${doc}")
            if (pandoc_pdf)
                add_pandoc_target ("${name}.pdf" "${doc}")
            endif ()
        endforeach ()

        add_pandoc_target ("all.html" ${docs})
        if (pandoc_pdf)
            add_pandoc_target ("all.pdf" ${docs})
        endif ()
    elseif (skip-missing)
        message (STATUS "Dependency check for option 'documentation' failed, skipping!")
    else ()
        message (FATAL_ERROR "Dependency check for option 'documentation' failed, stopping!")
    endif ()
endif ()
