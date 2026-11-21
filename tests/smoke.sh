#!/bin/sh
set -eu

output=$(
    printf '%s\n' \
        'version' \
        'export NOVA_TEST_VALUE=works' \
        'echo $NOVA_TEST_VALUE' \
        'alias greet=echo' \
        'greet hello' \
        'echo "quoted words"' \
        'echo "hello world" > .nova-smoke-output' \
        'cat < .nova-smoke-output | grep "hello world" && echo pipeline-ok' \
        'false && echo should-not-appear || echo conditional-ok' \
        'sleep 0.1 &' \
        'jobs' \
        'wait' \
        'exit' | ./nova
)

printf '%s\n' "$output" | grep -F 'NOVA Terminal 0.2.0-dev' >/dev/null
printf '%s\n' "$output" | grep -F 'works' >/dev/null
printf '%s\n' "$output" | grep -F 'hello' >/dev/null
printf '%s\n' "$output" | grep -F '[1]' >/dev/null
printf '%s\n' "$output" | grep -F 'quoted words' >/dev/null
printf '%s\n' "$output" | grep -F 'pipeline-ok' >/dev/null
printf '%s\n' "$output" | grep -F 'conditional-ok' >/dev/null
if printf '%s\n' "$output" | grep -F 'should-not-appear' >/dev/null; then
    echo 'conditional execution failed' >&2
    exit 1
fi
rm -f .nova-smoke-output .nova-smoke-env

echo "NOVA smoke tests passed."
