"""The serial dispatcher's routing contract: order, literals and authentication checks.

Fails when process_command() no longer matches tests/data/serial_command_routes.tsv. That is the point: a refactor of
the dispatcher must leave the table untouched, and an intended change regenerates it in the same commit
(python tools/serial_command_routes.py --write, then --golden) so the diff shows exactly which routes moved.
"""
import pathlib
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import serial_command_routes as routes  # noqa: E402

SAMPLE = r'''
void process_command(char* command) {
  if (strncmp(command, "FT8", 3) == 0 && (command[3] == '\0' || command[3] == ' ')) {
    process_ft8_command(command + 3);
    return;
  }
  // if (strcmp(command, "COMMENTED") == 0) { }
  if (authenticated) last_ping_ms = millis();
  if (strcmp(command, "STATUS") == 0) {
    Serial.println("has a } brace and \"quote\" in a string");
    return;
  }
#if ORCSDR_ORCDIAL
  if (!strcmp(command, "DIAL_FORGET")) {
    if (!authenticated) { Serial.println("DIAL_ERROR auth_required"); return; }
    forget();
    return;
  }
#endif
  if (helper::handle(command, authenticated)) return;
  const bool capture = strcmp(command, "CAPTURE") == 0;
  if (capture && authenticated) { start(); return; }
  if (strncmp(command, "SD_REMOVE ", 10) == 0) {
    sd_remove(command + 10);
    return;
  }
  if (strncmp(command, "GAIN ", 5) == 0) {
    apply(command + 5);
    if (!authenticated) { Serial.println("late"); return; }
    return;
  }
  if (strcmp(command, "PING") == 0 && authenticated) { pong(); }
}
void after() { if (strcmp(command, "NOT_A_ROUTE") == 0) {} }
'''

EXPECTED = [
    ("-", 'prefix[3]:"FT8"', "none", "-"),
    ("-", "if:authenticated", "condition", "-"),
    ("-", 'exact:"STATUS"', "none", "-"),
    ("#if ORCSDR_ORCDIAL", 'exact:"DIAL_FORGET"', "gate-first", "DIAL_ERROR auth_required"),
    ("-", "if:helper::handle(command, authenticated)", "condition", "-"),
    ("-", 'stmt:const bool capture = strcmp(command, "CAPTURE") == 0;', "none", "-"),
    ("-", "if:capture && authenticated", "condition", "-"),
    ("-", 'prefix[10]:"SD_REMOVE "', "none", "-"),
    ("-", 'prefix[5]:"GAIN "', "inside", "-"),
    ("-", 'exact:"PING"', "condition", "-"),
]

found = routes.extract(SAMPLE)
got = [(r["guard"], r["match"], r["auth"], r["deny"]) for r in found]
assert got == EXPECTED, "\n" + "\n".join(f"{a}\n{b}" for a, b in zip(got, EXPECTED) if a != b) + f"\n{len(got)} rows"

# Order decides which route a line reaches: a three-character prefix placed first takes everything that starts with it.
assert routes.first_literal_route(found, "FT8 STATUS") == 0
assert found[routes.first_literal_route(found, "SD_REMOVE 00")]["auth"] == "none"
assert routes.first_literal_route(found, "UNKNOWN") == -1
# Only a route that refuses before doing anything else yields an input that is safe to send unauthenticated.
assert routes.golden_rows(found) == [], "a guarded route (#if) must not produce a golden row"
unguarded = routes.extract(SAMPLE.replace("#if ORCSDR_ORCDIAL\n", "").replace("#endif\n", ""))
assert routes.golden_rows(unguarded) == [("DIAL_FORGET", "DIAL_ERROR auth_required")]

# CRLF checkouts give the same table.
assert [r["match"] for r in routes.extract(SAMPLE.replace("\n", "\r\n"))] == [r["match"] for r in found]

sys.argv = [sys.argv[0], "--check"]
status = routes.main()
assert status == 0, "process_command no longer matches the recorded route table (see DRIFT above)"
print("PASS: extractor rules hold on the sample dispatcher; recorded serial routes match main.cpp")
