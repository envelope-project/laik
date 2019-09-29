dir="`dirname -- "${0}"`"
exe=${dir}/../src/kvstest
export LAIK_BACKEND=tcp
export LAIK_TCP_CONFIG=${dir}/4ranks.conf
(${exe} & ${exe} & ${exe} & ${exe}) > kvstest.out
cmp kvstest.out "${dir}/../mpi/test-kvstest-mpi-4.expected"

