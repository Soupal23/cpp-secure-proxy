
#!/usr/bin/env bash

set -u

PROXY="http://127.0.0.1:8080"
PASS=0
FAIL=0

check_status() {
    local name="$1"
    local expected="$2"
    shift 2

    local actual
    actual=$(curl -sS --max-time 20 \
        -o /dev/null -w '%{http_code}' "$@" 2>/dev/null) || true

    if [[ "$actual" == "$expected" ]]; then
        echo "PASS: $name (HTTP $actual)"
        ((PASS+=1))
    else
        echo "FAIL: $name (expected $expected, got ${actual:-empty})"
        ((FAIL+=1))
    fi
}

echo "Testing proxy at $PROXY"
echo

# Allowed HTTP destination: Google normally redirects.
check_status "Allowed HTTP request" "301" \
    -x "$PROXY" "http://google.com/"

# Blocked HTTP destination.
check_status "Blocked HTTP request" "403" \
    -x "$PROXY" "http://example.com/"

# Allowed HTTPS destination: inspect the origin response.
check_status "Allowed HTTPS request" "301" \
    -x "$PROXY" "https://google.com/"

# Unsupported method should return 501.
check_status "Unsupported HTTP method" "501" \
    -X POST -x "$PROXY" "http://google.com/"

blocked_https=$(curl -sS --max-time 15 \
    -x "$PROXY" \
    -o /dev/null \
    -w '%{http_connect}' \
    https://example.com/ 2>/dev/null) || true

if [[ "$blocked_https" == "403" ]]; then
    echo "PASS: Blocked HTTPS request (CONNECT $blocked_https)"
    ((PASS+=1))
else
    echo "FAIL: Blocked HTTPS request (expected CONNECT 403, got ${blocked_https:-empty})"
    ((FAIL+=1))
fi

check_status "Unreachable upstream" "502" \
    -x "$PROXY" "http://127.0.0.1:1/"

echo
echo "Results: $PASS passed, $FAIL failed"
[[ "$FAIL" -eq 0 ]]


