#!/usr/bin/env bash
# Host tests for the rtl_tcp client and server: protocol, source policy, and the
# connection loops against fake peers on loopback. Needs g++ with sanitizers and
# pthreads. These are host simulations; they say nothing about Tab5 throughput.
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
app="$root/apps/orcsdr-tab5"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread "-I$app/ui")

g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/tests/rtl_tcp_protocol_host_test.cpp" -o "$out/protocol"
g++ "${flags[@]}" "$app/ui/receiver_source_policy.cpp" "$app/tests/receiver_source_policy_host_test.cpp" -o "$out/policy"
g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/ui/rtl_tcp_session.cpp" \
  "$app/tests/rtl_tcp_session_host_test.cpp" -o "$out/client"
g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/ui/rtl_tcp_server_session.cpp" \
  "$app/tests/rtl_tcp_server_session_host_test.cpp" -o "$out/server"

g++ "${flags[@]}" "$app/tests/rtl_tcp_iq_ring_host_test.cpp" -o "$out/ring"

"$out/protocol"
"$out/ring"
"$out/policy"
"$out/client"
"$out/server"
