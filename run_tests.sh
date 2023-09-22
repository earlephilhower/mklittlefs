#!/bin/bash

set -o errexit
set -o nounset
shopt -s extglob

if [[ $# != 1 ]]; then
    echo "Usage: ${0} <test_dir>"
    exit 1
fi

tests_dir=$(cd ${1} && pwd -P)
work_dir=$(mktemp -d)

if [[ ! "$work_dir" || ! -d "$work_dir" ]]; then
    echo "Failed to create working dir"
    exit 1
fi

function cleanup {
    rm -rf "$work_dir"
}
trap cleanup EXIT

export MKLITTLEFS="$(pwd -P)/mklittlefs"

cd $work_dir

tests_executed=0
tests_failed=0

for test_case in ${tests_dir}/!(*@(.out|.err)); do
    test_name=$(basename "${test_case}")
    expected_out=/dev/null
    expected_err=/dev/null

    if [[ -f "${test_case}.out" ]]; then
        expected_out="${test_case}.out"
    fi
    if [[ -f "${test_case}.err" ]]; then
        expected_err="${test_case}.err"
    fi

    mkdir "${test_name}"
    actual_out="${test_name}/actual.out"
    actual_err="${test_name}/actual.err"

    failed=0
    rc=0
    WORKDIR="${work_dir}/${test_name}" $test_case > "${actual_out}" 2> "${actual_err}" || rc=$?
    if [[ ${rc} != 0 ]]; then
        echo "Test ${test_name} failed with exit code ${rc}"
        failed=1
    fi

    rc=0
    diff -q "${expected_out}" "${actual_out}" || rc=$?
    if [[ ${rc} != 0 ]]; then
        diff "${expected_out}" "${actual_out}" | head
        failed=1
    fi

    rc=0
    diff -q "${expected_err}" "${actual_err}" || rc=$?
    if [[ ${rc} != 0 ]]; then
        diff "${expected_err}" "${actual_err}" | head
        failed=1
    fi

    if [[ $failed != 0 ]]; then
        ((++tests_failed))
    fi
    ((++tests_executed))
done

echo "Executed ${tests_executed} test cases, ${tests_failed} failures"
exit $tests_failed
