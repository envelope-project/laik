if enable_doc
    docs = ['index.md', 'tutorial-jacobi2d.md', 'Debugging.md', 'TODO.md']

    foreach doc : docs
        custom_target (
            'HTML version of ' + doc,
            build_by_default: true,
            command : [program_pandoc, '--output', '@OUTPUT@', '--', '@INPUT@'],
            input: doc,
            output : '@BASENAME@.html',
        )

        custom_target (
            'PDF version of ' + doc,
            build_by_default: true,
            command : [program_pandoc, '--output', '@OUTPUT@', '--', '@INPUT@'],
            input: doc,
            output : '@BASENAME@.pdf',
        )
    endforeach

    custom_target (
        'All-in-one HTML documentation',
        build_by_default: true,
        command : [program_pandoc, '--output', '@OUTPUT@', '--', '@INPUT@'],
        input: docs,
        output : 'all.html',
    )

    custom_target (
        'All-in-one PDF documentation',
        build_by_default: true,
        command : [program_pandoc, '--output', '@OUTPUT@', '--', '@INPUT@'],
        input: docs,
        output : 'all.pdf',
    )
endif
