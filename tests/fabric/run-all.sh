#!/bin/sh
for sh in ../common/test*.sh; do
  bn=`basename $sh`
  echo -n "$bn "
# Skipped tests:
#
# These depend on fabric_sync():
#   test-kvstest
#   test-location
#   test-spaces
# which would be needed for KVS and is not implemented yet.
#
# test-spmv2-shrink gets stuck indefinitely. It might be related to
# fabric_resize() not supporting shrinking yet.
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
done
