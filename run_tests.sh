#!/bin/bash

# This is a main testing script for:
#	* regression tests
# Copyright (c) 2018, Postgres Professional

set -eux

echo CHECK_CODE=$CHECK_CODE

status=0

# perform code analysis if necessary
if [ "$CHECK_CODE" = "clang" ]; then
    scan-build --status-bugs make USE_PGXS=1 || status=$?
    exit $status

elif [ "$CHECK_CODE" = "cppcheck" ]; then
    cppcheck \
		--template "{file} ({line}): {severity} ({id}): {message}" \
        --enable=warning,portability,performance \
        --suppress=redundantAssignment \
        --suppress=uselessAssignmentPtrArg \
		--suppress=literalWithCharPtrCompare \
        --suppress=incorrectStringBooleanError \
        --std=c99 *.c *.h 2> cppcheck.log

    if [ -s cppcheck.log ]; then
        cat cppcheck.log
        status=1 # error
    fi

    exit $status
fi

# don't forget to "make clean"
make USE_PGXS=1 clean

# initialize database
initdb

# build extension
make USE_PGXS=1 install

# check build
status=$?
if [ $status -ne 0 ]; then exit $status; fi

echo "port = 55435" >> $PGDATA/postgresql.conf
pg_ctl start -l /tmp/postgres.log -w

# check startup
status=$?
if [ $status -ne 0 ]; then cat /tmp/postgres.log; fi

# run regression tests
export PG_REGRESS_DIFF_OPTS="-w -U3" # for alpine's diff (BusyBox)
PGPORT=55435 make USE_PGXS=1 installcheck || status=$?

# show diff if it exists
if test -f regression.diffs; then cat regression.diffs; fi

exit $status
