#!/bin/sh
${LAUNCHER-./launcher} -n 4 ../src/locationtest | LC_ALL='C' sort > test-location-4.out
#cmp test-location-4.out "$(dirname -- "${0}")/test-location-4.expected"
