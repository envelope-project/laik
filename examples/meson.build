foreach example : [
    'jac1d',
    'jac2d',
    'jac2d-ser',
    'jac3d',
    'markov',
    'markov2',
    'markov-ser',
    'propagation1d',
    'propagation2d',
    'spmv',
    'spmv2',
    'vsum',
    'vsum2',
]
    set_variable (
        'example_' + example.underscorify(),
         executable (
            example,
            example + '.c',
            c_args : ['-fopenmp', '-Wno-unused-but-set-variable', '-Wno-unused-variable'],
            dependencies : [libgomp, liblaik, libm],
        )
    )
endforeach

if enable_cpp
    executable (
        'raytracer',
        'raytracer.cpp',
        cpp_args : ['-Wno-unused-variable'],
        dependencies : [liblaik],
    )
endif
