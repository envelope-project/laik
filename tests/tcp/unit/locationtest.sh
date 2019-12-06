dir="`dirname -- "${0}"`"
exe=${dir}/../../src/locationtest
export LAIK_BACKEND=tcp
export LAIK_TCP_CONFIG=${dir}/../4ranks.conf
(${exe} & ${exe} & ${exe} & ${exe}) > locationtest.out

