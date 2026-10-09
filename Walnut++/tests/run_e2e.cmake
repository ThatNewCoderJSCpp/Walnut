cmake_minimum_required(VERSION 3.18)
get_filename_component(name "${CASE}" NAME_WE)
get_filename_component(dir "${CASE}" DIRECTORY)
set(base "${dir}/${name}")
file(MAKE_DIRECTORY "${WORK}/e2e-${name}")

if(EXISTS "${base}.err")
    file(READ "${base}.err" expected_error)
    string(STRIP "${expected_error}" expected_error)
    execute_process(COMMAND "${WALNUT}" emit "${CASE}" -o "${WORK}/e2e-${name}/out.cpp"
        RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)
    if(rc EQUAL 0)
        message(FATAL_ERROR "expected a compile error containing '${expected_error}', but compilation succeeded")
    endif()
    string(FIND "${out}${err}" "${expected_error}" pos)
    if(pos EQUAL -1)
        message(FATAL_ERROR "expected an error containing '${expected_error}', got:\n${out}${err}")
    endif()
    return()
endif()

set(input_args)
if(EXISTS "${base}.in")
    set(input_args INPUT_FILE "${base}.in")
endif()

execute_process(COMMAND "${WALNUT}" build "${CASE}" -O0 -o "${WORK}/e2e-${name}/program"
    RESULT_VARIABLE build_rc OUTPUT_VARIABLE build_out ERROR_VARIABLE build_err)
if(NOT build_rc EQUAL 0)
    message(FATAL_ERROR "build failed (${build_rc}):\n${build_out}${build_err}")
endif()

set(program_args)
if(EXISTS "${base}.args")
    file(STRINGS "${base}.args" program_args)
endif()

execute_process(COMMAND "${WORK}/e2e-${name}/program" ${program_args} ${input_args}
    RESULT_VARIABLE rc OUTPUT_VARIABLE out ERROR_VARIABLE err)

set(expected_rc 0)
if(EXISTS "${base}.rc")
    file(READ "${base}.rc" expected_rc)
    string(STRIP "${expected_rc}" expected_rc)
endif()

file(READ "${base}.out" expected)
set(actual "${out}${err}")
string(REPLACE "\r\n" "\n" actual "${actual}")

if(NOT actual STREQUAL expected)
    message(FATAL_ERROR "output mismatch\n--- expected ---\n${expected}--- actual ---\n${actual}")
endif()

if(NOT rc STREQUAL expected_rc)
    message(FATAL_ERROR "exit status ${rc}, expected ${expected_rc}")
endif()
