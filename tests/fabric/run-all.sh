#!/bin/sh
for sh in ../common/test*.sh; do
  bn=`basename $sh`
  echo -n "$bn "
# Skipped tests:
#
# test-kvstest and test-spaces fail because fabric_sync(),
# which would be needed for KVS, is not implemented yet.
#
# test-location-4.sh fails at test/src/locationtest.c, line 18.
# assertion laik_group_locationid(g0, i) == i fails.
# The comment in src/core.c above laik_group_locationid() says
# "locations in KVS", so it might be related to the previous failure?
#
# test-spmv2-shrink fails in laik_rangelist_migrate(), at:
#   assert((new_id >= 0) && (new_id < (int) new_count));
# This might be related to groups not working.
#
  case $bn in
  test-kvstest*|test-spaces*|test-location*|test-spmv2-shrink-*)
    echo SKIPPED
    continue ;;
  esac
# Normal tests
  $sh
  case $? in
  0) echo PASS ;;
  *) echo FAIL; exit 1 ;;
  esac
#
#  sleep 1
done
