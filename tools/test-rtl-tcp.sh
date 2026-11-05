#!/usr/bin/env bash
# Host tests for the rtl_tcp client and server: protocol, source policy, the
# connection loops against fake peers on loopback, and the Settings rows against
# the repo's display stub. Needs g++ with sanitizers and pthreads. These are host
# simulations; they say nothing about Tab5 throughput or how the panel looks.
#
# Optional: RTL_TCP_REPLAY_FILE=<raw cu8 IQ recording> also replays a real
# recording through the client and checks the bytes and an FM demod are unchanged.
#   rtl_sdr -f 96100000 -s 960000 -n 7680000 fm.cu8   # capture one, then:
#   RTL_TCP_REPLAY_FILE=fm.cu8 bash tools/test-rtl-tcp.sh
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
app="$root/apps/orcsdr-tab5"
out="$(mktemp -d)"
trap 'rm -rf "$out"' EXIT
flags=(-std=c++17 -Wall -Wextra -Werror -fsanitize=address,undefined -pthread "-I$app/ui")

g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/tests/rtl_tcp_protocol_host_test.cpp" -o "$out/protocol"
g++ "${flags[@]}" "$app/tests/rtl_tcp_iq_ring_host_test.cpp" -o "$out/ring"
g++ "${flags[@]}" "$app/ui/receiver_source_policy.cpp" "$app/tests/receiver_source_policy_host_test.cpp" -o "$out/policy"
g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/ui/rtl_tcp_session.cpp" \
  "$app/tests/rtl_tcp_session_host_test.cpp" -o "$out/client"
g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/ui/rtl_tcp_server_session.cpp" \
  "$app/tests/rtl_tcp_server_session_host_test.cpp" -o "$out/server"
g++ "${flags[@]}" "$app/ui/rtl_tcp_protocol.cpp" "$app/ui/rtl_tcp_session.cpp" \
  "$app/tests/rtl_tcp_replay_host_test.cpp" -o "$out/replay"
# The Settings screen carries older format warnings of its own, so this one is not -Werror.
g++ -std=c++17 -UNDEBUG -Wall -Wno-format-truncation -fsanitize=address,undefined \
  "-I$app/tests/maps/ui_stubs" "-I$app/ui" "$app/tests/settings_remote_ui_tests.cpp" \
  "$app/ui/settings_app.cpp" "$app/ui/text_editor.cpp" "$app/ui/focus_nav.cpp" \
  "$app/ui/keyboard_input.cpp" "$app/ui/clock_settings.cpp" "$app/ui/screen_controller.cpp" \
  "$app/ui/receiver_location.cpp" -o "$out/settings_ui"

"$out/protocol"
"$out/ring"
"$out/policy"
"$out/client"
"$out/server"
"$out/settings_ui"
if [[ -n "${RTL_TCP_REPLAY_FILE:-}" ]]; then
  "$out/replay" "$RTL_TCP_REPLAY_FILE" 1
  "$out/replay" "$RTL_TCP_REPLAY_FILE" 7
else
  "$out/replay"
fi
