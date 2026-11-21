#!/bin/sh
set -eu

output=$(
    printf '%s\n' \
        'version' \
        'export NOVA_TEST_VALUE=works' \
        'echo $NOVA_TEST_VALUE' \
        'alias greet=echo hello' \
        'greet' \
        'sleep 0.1 &' \
        'jobs' \
        'wait' \
        'exit' | ./nova
)

printf '%s\n' "$output"
printf '%s\n' "$output" | grep -F 'NOVA Terminal 0.2.0-dev' >/dev/null
printf '%s\n' "$output" | grep -F 'works' >/dev/null
printf '%s\n' "$output" | grep -F 'hello' >/dev/null
printf '%s\n' "$output" | grep -F '[1]' >/dev/null

echo "NOVA smoke tests passed."
