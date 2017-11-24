if enable_mpi
    foreach test : [
        ['test-mpi-jac1d-repart'     , 'test-jac1d-1000-repart.expected' , [example_jac1d, '1000', '50', '10']],
        ['test-mpi-jac1d'            , 'test-jac1d-100.expected'         , [example_jac1d, '100']],
        ['test-mpi-jac2d-noc'        , 'test-jac2dn-1000.expected'       , [example_jac2d, '-s', '-n', '1000']],
        ['test-mpi-jac2d'            , 'test-jac2d-1000.expected'        , [example_jac2d, '-s', '1000']],
        ['test-mpi-jac3d-noc'        , 'test-jac3dn-100.expected'        , [example_jac3d, '-s', '-n', '100']],
        ['test-mpi-jac3d'            , 'test-jac3d-100.expected'         , [example_jac3d, '-s', '100']],
        ['test-mpi-markov2'          , 'test-markov2-40-4.expected'      , [example_markov2, '40', '4']],
        ['test-mpi-markov'           , 'test-markov-40-4.expected'       , [example_markov, '40', '4']],
        ['test-mpi-mpi-vsum'         , 'test-vsum.expected'              , [example_vsum]],
        ['test-mpi-spmv2r'           , 'test-spmv2.expected'             , [example_spmv2, '-r', '10', '3000']],
        ['test-mpi-spmv2-shrink-inc' , 'test-spmv2.expected'             , [example_spmv2, '-s', '-i', '10', '3000']],
        ['test-mpi-spmv2-shrink'     , 'test-spmv2.expected'             , [example_spmv2, '-s', '2', '10', '3000']],
        ['test-mpi-spmv2'            , 'test-spmv2.expected'             , [example_spmv2, '10', '3000']],
        ['test-mpi-spmv'             , 'test-spmv.expected'              , [example_spmv, '4000']],
        ['test-mpi-vsum2'            , 'test-vsum2.expected'             , [example_vsum2]],
    ]
        expected = files (test[1])[0]

        output = custom_target (
            test[0] + '.output',
            command : [program_mpirun, '-np', '4'] + test[2],
            capture : true,
            output : test[0] + '.output',
        )

        sorted = custom_target (
            test[0] + '.sorted',
            command : [program_sort, output],
            capture : true,
            output : test[0] + '.sorted',
        )

        test (
            test[0],
            program_cmp,
            args : [sorted, expected],
        )
    endforeach
endif
