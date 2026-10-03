#!/usr/bin/env bash
set -euo pipefail
cd "$(dirname "$0")/../.."
work_dir=$(mktemp -d)
trap 'rm -rf "$work_dir"' EXIT
g++ -std=c++14 -Wall -Wextra -Werror -Isrc tests/chat_translate/protocol_test.cpp -o "$work_dir/protocol_test"
"$work_dir/protocol_test"
printf '#define FEATURE_CHAT_TRANSLATE 1\n' > "$work_dir/feature_config.h"
g++ -std=c++14 -Wall -Wextra -Werror -pthread -I"$work_dir" -Isrc tests/chat_translate/backend_test.cpp src/features/chat_translate/backend.cpp -o "$work_dir/backend_test"
"$work_dir/backend_test"
