dir="`dirname -- "${0}"`"
exe=${dir}/../../src/spacestest
export LAIK_BACKEND=tcp
export LAIK_TCP_CONFIG=${dir}/../4ranks.conf
(${exe} & ${exe} & ${exe} & ${exe}) > spacestest.out

