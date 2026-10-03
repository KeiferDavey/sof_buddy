#!/bin/sh
set -eu
cd "$(dirname "$0")/../.."
out="$(mktemp /tmp/sofbuddy-radio-test.XXXXXX)"
trap 'rm -f "$out"' EXIT
c++ -std=c++14 -Wall -Wextra -Werror -Isrc tests/url_radio/protocol_test.cpp -o "$out"
"$out"
