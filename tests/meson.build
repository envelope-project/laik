foreach test : [
    ['test-jac1d-repart'  , 'test-jac1d-1000-repart.expected', [example_jac1d, '1000', '50', '10']],
    ['test-jac1d'         , 'test-jac1d-100.expected'        , [example_jac1d, '100']],
    ['test-jac2d'         , 'test-jac2d-1000.expected'       , [example_jac2d, '-s', '1000']],
    ['test-jac3d'         , 'test-jac3d-100.expected'        , [example_jac3d, '-s', '100']],
    ['test-markov2'       , 'test-markov2-20-4.expected'     , [example_markov2, '20', '4']],
    ['test-markov'        , 'test-markov-20-4.expected'      , [example_markov, '20', '4']],
    ['test-propagation2d' , 'test-propagation2d-10.expected' , [example_propagation2d, '10', '10']],
    ['test-spmv2r'        , 'test-spmv2.expected'            , [example_spmv2, '-r', '10', '3000']],
    ['test-spmv2'         , 'test-spmv2.expected'            , [example_spmv2, '10', '3000']],
    ['test-spmv'          , 'test-spmv.expected'             , [example_spmv, '4000']],
    ['test-vsum'          , 'test-vsum.expected'             , [example_vsum]],
    ['test-vsum2'         , 'test-vsum.expected'             , [example_vsum2]],
    ['test-vsum-log'      , 'test-vsum.expected'             , [program_env, 'LAIK_LOG=1:0', example_vsum]],
]
    expected = files (test[1])[0]

    output = custom_target (
        test[0] + '.output',
        command : test[2],
        capture : true,
        output : test[0] + '.output',
    )

    test (
        test[0],
        program_cmp,
        args : [output, expected],
    )
endforeach
