#!/usr/bin/env bash

# run ctest and capture its output
declare -r LOG_FILE='ctest.log'
ctest -T memcheck 2>&1 | tee "${LOG_FILE}"
EXIT="${PIPESTATUS[0]}"

# if any tests failed, cat their logfiles
cat ctest.log | \
	grep -A100 "The following tests FAILED:" | \
	grep -E '[   ]+[0-9]+ - ' | \
	sed 's/[^0-9]*//' | \
	cut -f1 -d' ' | \
	xargs -I {} cat Testing/Temporary/MemoryChecker.{}.log

# cleanup and exit
rm "${LOG_FILE}"
exit "$EXIT"
