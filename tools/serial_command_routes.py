#!/usr/bin/env python3
"""Extract the ordered top-level route table of the Tab5 serial dispatcher.

`process_command()` in apps/orcsdr-tab5/ui/main.cpp is a long chain of
`if (strcmp(command, "...") == 0) { ... return; }` statements. The ORDER of
those statements, the literals they match and whether they check
`authenticated` are the serial API's routing contract. This tool reads that
contract out of the source so a refactor can prove it did not change it.

  python tools/serial_command_routes.py --write    regenerate tests/data/serial_command_routes.tsv
  python tools/serial_command_routes.py --check    fail if the source no longer matches that file
  python tools/serial_command_routes.py --golden   regenerate tests/data/serial_golden_unauthenticated.tsv

Only statements at the top level of the function body are routes. Columns:
  order     position among the top-level statements (1-based)
  guard     enclosing preprocessor conditions, outermost first, '-' when none
  match     how `command` is compared, e.g. exact:"PING" or prefix[10]:"SD_REMOVE " (several joined by ' | ');
            if:<condition> for a test with no literal (a delegated handler); stmt:<text> for any other statement
  auth      none        the route never mentions `authenticated`
            condition   `authenticated` is part of the route's own condition (no reply when it is false)
            gate-first  the first statement of the body rejects unauthenticated callers
            inside      `authenticated` is tested somewhere later in the body
  deny      the reply printed by a gate-first route, '-' otherwise
This is a reading of the text, not a compile: `auth=none` says the dispatcher
itself does not check, not that a called handler does not.
"""
from __future__ import annotations

import argparse
import pathlib
import re
import sys

ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE = ROOT / "apps/orcsdr-tab5/ui/main.cpp"
TABLE = ROOT / "tests/data/serial_command_routes.tsv"
GOLDEN = ROOT / "tests/data/serial_golden_unauthenticated.tsv"
SIGNATURE = "void process_command(char* command) {"

COMPARE = re.compile(
    r'(?P<neg>!\s*)?(?P<fn>strn?cmp|strn?casecmp)\(\s*command\s*(?P<off>\+\s*\d+)?\s*,\s*'
    r'"(?P<lit>(?:[^"\\]|\\.)*)"\s*(?:,\s*(?P<len>\d+)\s*)?\)')
DENY = re.compile(r'(?:Serial\.(?:println|printf|print)|hooks\.write_line)\(\s*"((?:[^"\\]|\\.)*)"')

# Command families moved out of process_command(). Each is called from the dispatcher as
#   if (<namespace>::handle_command(command, authenticated, <hooks>)) return;
# and its own top-level routes are read from FAMILY_SIGNATURE in the file named here.
FAMILIES = {
    "orcsdr::serial_help": "apps/orcsdr-tab5/ui/serial_help.cpp",
    "orcsdr::serial_dsp": "apps/orcsdr-tab5/ui/serial_dsp.cpp",
    "orcsdr::serial_memory": "apps/orcsdr-tab5/ui/serial_memory.cpp",
}
FAMILY_SIGNATURE = "bool handle_command(const char* command, bool authenticated, const Hooks& hooks) {"
FAMILY_CALL = re.compile(r"([\w:]+)::handle_command\(command, authenticated, \w+\)")
FAMILY_PLUMBING = re.compile(r"return false;|\(void\)\w+;")


def blank_comments_and_strings(text: str, keep_strings: bool) -> str:
    """Same length as `text`; comments (and optionally string/char contents) replaced by spaces."""
    out = list(text)
    i, n = 0, len(text)
    while i < n:
        two = text[i:i + 2]
        if two == "//":
            j = text.find("\n", i)
            j = n if j < 0 else j
            out[i:j] = " " * (j - i)
            i = j
        elif two == "/*":
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            for k in range(i, j):
                if text[k] != "\n":
                    out[k] = " "
            i = j
        elif text[i] in "\"'":
            quote = text[i]
            j = i + 1
            while j < n and text[j] != quote:
                j += 2 if text[j] == "\\" else 1
            if not keep_strings:
                out[i + 1:j] = " " * (j - i - 1)
            i = j + 1
        else:
            i += 1
    return "".join(out)


def matching(text: str, start: int, open_ch: str, close_ch: str) -> int:
    """Index of the bracket closing the one at `start` (text must have strings blanked)."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == open_ch:
            depth += 1
        elif text[i] == close_ch:
            depth -= 1
            if depth == 0:
                return i
    raise SystemExit(f"unbalanced {open_ch}{close_ch} from offset {start}")


def squeeze(code_text: str) -> str:
    """One line, single spaces, no tabs: safe as a TSV field and stable against re-indentation."""
    return " ".join(code_text.split())


def describe_matches(condition: str) -> list[str]:
    found = []
    for m in COMPARE.finditer(condition):
        fn, lit, length, off = m["fn"], m["lit"], m["len"], m["off"]
        where = f"+{off[1:].strip()}" if off else ""
        if fn.startswith("strn"):
            found.append(f'prefix{where}[{length}]:"{lit}"')
        else:
            found.append(f'exact{where}:"{lit}"')
    return found


def read_family(namespace: str) -> str:
    return (ROOT / FAMILIES[namespace]).read_text(encoding="utf-8", errors="replace")


def extract(source_text: str, families=None) -> list[dict]:
    """The dispatcher's ordered top-level statements, with every registered family's routes spliced in where the
    dispatcher calls it. `families` maps a namespace to that family's source text (tests); default: read FAMILIES."""
    load = (lambda ns: families[ns]) if families is not None else read_family
    known = families if families is not None else FAMILIES
    return extract_function(source_text, SIGNATURE, False, known, load)


def extract_function(source_text: str, signature: str, is_family: bool, known, load) -> list[dict]:
    text = source_text.replace("\r\n", "\n")
    start = text.find(signature)
    if start < 0 or text.find(signature, start + 1) >= 0:
        raise SystemExit(f"definition not found exactly once: {signature}")
    code = blank_comments_and_strings(text, keep_strings=False)   # structure only
    readable = blank_comments_and_strings(text, keep_strings=True)  # literals kept, comments gone
    body_open = start + len(signature) - 1
    body_close = matching(code, body_open, "{", "}")

    routes: list[dict] = []
    guards: list[str] = []
    i = body_open + 1
    while i < body_close:
        ch = code[i]
        if ch in " \t\n":
            i += 1
            continue
        line_end = code.find("\n", i)
        if ch == "#":
            directive = readable[i:line_end].strip()
            word = re.match(r"#\s*(\w+)", directive)[1]
            if word in ("if", "ifdef", "ifndef"):
                guards.append(re.sub(r"\s+", " ", directive))
            elif word in ("else", "elif"):
                guards[-1] = guards[-1] + " /" + re.sub(r"\s+", " ", directive)
            elif word == "endif":
                guards.pop()
            i = line_end + 1
            continue
        keyword = re.match(r"(else\s+)?if\s*\(", code[i:])
        if keyword:
            cond_open = i + keyword.end() - 1
            cond_close = matching(code, cond_open, "(", ")")
            j = cond_close + 1
            while code[j] in " \t\n":
                j += 1
            if code[j] == "{":
                body_end = matching(code, j, "{", "}")
                body = readable[j + 1:body_end]
                body_code = code[j + 1:body_end]
            else:
                body_end = code.find(";", j)
                body = readable[j:body_end + 1]
                body_code = code[j:body_end + 1]
            condition = readable[cond_open + 1:cond_close]
            matches = describe_matches(condition)
            call = FAMILY_CALL.fullmatch(squeeze(condition))
            if call and call[1] in known and not is_family:
                # A family moved out of the dispatcher: its routes count as if they were still written here.
                if squeeze(body) != "return;":
                    raise SystemExit(f"family call {call[1]} must be followed by a bare 'return;'")
                for r in extract_function(load(call[1]), FAMILY_SIGNATURE, True, known, load):
                    here = list(guards) + ([r["guard"]] if r["guard"] != "-" else [])
                    routes.append(dict(r, guard=" && ".join(here) if here else "-",
                                       line=text.count("\n", 0, i) + 1))
                i = body_end + 1
                continue
            auth, deny = classify(condition, body, body_code)
            routes.append({
                "guard": " && ".join(guards) if guards else "-",
                # A condition with no literal is still part of the order: a delegated handler or a state test.
                "match": " | ".join(matches) if matches else "if:" + squeeze(condition),
                "auth": auth,
                "deny": deny,
                "line": text.count("\n", 0, i) + 1,
            })
            i = body_end + 1
            continue
        # Any other top-level statement (a local that later routes test, a block): recorded so its position is pinned.
        if ch == "{":
            end = matching(code, i, "{", "}")
        else:
            semi = code.find(";", i)
            brace = code.find("{", i)
            end = matching(code, brace, "{", "}") if 0 <= brace < semi else semi
        statement = readable[i:end + 1]
        if is_family and FAMILY_PLUMBING.fullmatch(squeeze(statement)):
            i = end + 1   # not routing: the family's "not mine" result and unused-parameter marks
            continue
        routes.append({
            "guard": " && ".join(guards) if guards else "-",
            "match": "stmt:" + squeeze(statement),
            "auth": "condition" if "authenticated" in code[i:end + 1] else "none",
            "deny": "-",
            "line": text.count("\n", 0, i) + 1,
        })
        i = end + 1
    if guards:
        raise SystemExit("unbalanced preprocessor conditionals inside process_command")
    return routes


def classify(condition: str, body: str, body_code: str) -> tuple[str, str]:
    if "authenticated" in condition:
        return "condition", "-"
    if "authenticated" not in body_code:
        return "none", "-"
    first = re.match(r"\s*if\s*\(", body_code)
    if first:
        close = matching(body_code, first.end() - 1, "(", ")")
        gate = body_code[first.end():close]
        if re.fullmatch(r"\s*!\s*authenticated\s*", gate):
            k = close + 1
            while body_code[k] in " \t\n":
                k += 1
            end = matching(body_code, k, "{", "}") if body_code[k] == "{" else body_code.find(";", body_code.find(";", k) + 1)
            branch = body[k:end + 1]
            # `return;` in the dispatcher, `return true;` in a family: both end the route with the line consumed.
            if re.search(r"\breturn(\s+true)?\s*;", blank_comments_and_strings(branch, keep_strings=False)):
                reply = DENY.search(branch)
                return "gate-first", (reply[1] if reply else "-")
    return "inside", "-"


def render(routes: list[dict]) -> str:
    lines = ["# Generated by tools/serial_command_routes.py --write. Do not edit by hand.",
             "order\tguard\tmatch\tauth\tdeny"]
    for n, r in enumerate(routes, 1):
        lines.append(f'{n}\t{r["guard"]}\t{r["match"]}\t{r["auth"]}\t{r["deny"]}')
    return "\n".join(lines) + "\n"


def literal_hit(match: str, line: str) -> bool:
    """Would a route's literal comparison accept `line`? Unsupported forms (offsets, escapes) count as a hit, so
    callers that use this to prove a line is NOT intercepted stay on the safe side."""
    for part in match.split(" | "):
        m = re.fullmatch(r'(exact|prefix\[(\d+)\]):"(.*)"', part)
        if not m or chr(92) in m[3]:
            return True
        if m[1] == "exact":
            if line == m[3]:
                return True
        else:
            # strncmp(command, literal, n): equal over the first n characters, stopping at either terminator.
            n = int(m[2])
            if (line + chr(0))[:n] == (m[3] + chr(0))[:n]:
                return True
    return False


def first_literal_route(routes: list[dict], line: str) -> int:
    """Index of the first literal route that would accept `line`, -1 when none does."""
    for index, r in enumerate(routes):
        if r["match"].startswith(("if:", "stmt:")):
            continue
        if literal_hit(r["match"], line):
            return index
    return -1


def golden_rows(routes: list[dict]) -> list[tuple[str, str]]:
    """Input lines that are safe to send unauthenticated: single-literal routes whose first act is to refuse, and
    which no earlier literal route would take first."""
    rows = []
    seen = set()
    for index, r in enumerate(routes):
        if r["auth"] != "gate-first" or r["deny"] == "-" or r["guard"] != "-" or " | " in r["match"]:
            continue
        if "%" in r["deny"]:
            continue
        m = re.fullmatch(r'(exact|prefix\[(\d+)\]):"(.*)"', r["match"])
        if not m:
            continue
        literal = m[3]
        if "\\" in literal:
            continue
        if m[1] == "exact":
            line = literal
        else:
            if int(m[2]) != len(literal):
                continue   # the compared length is not the literal's length: not a plain prefix
            line = literal + ("0" if literal.endswith(" ") else "")
        reply = r["deny"]
        reply = reply[:-2] if reply.endswith("\\n") else reply
        if line in seen or first_literal_route(routes, line) != index:
            continue
        seen.add(line)
        rows.append((line, reply))
    return rows


def render_golden(rows: list[tuple[str, str]]) -> str:
    lines = ["# Generated by tools/serial_command_routes.py --golden. Do not edit by hand.",
             "# Each input is refused by the dispatcher's first statement when the session is NOT authenticated,",
             "# so sending it changes nothing on the device. Expected reply is exact, one line.",
             "input\texpected_reply"]
    lines += [f"{a}\t{b}" for a, b in rows]
    return "\n".join(lines) + "\n"


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument("--write", action="store_true")
    mode.add_argument("--check", action="store_true")
    mode.add_argument("--golden", action="store_true")
    mode.add_argument("--list", action="store_true", help="print the table with current source line numbers")
    args = parser.parse_args()

    routes = extract(SOURCE.read_text(encoding="utf-8", errors="replace"))
    table = render(routes)
    golden = render_golden(golden_rows(routes))
    if args.list:
        for n, r in enumerate(routes, 1):
            print(f'{n}\t{r["line"]}\t{r["guard"]}\t{r["match"]}\t{r["auth"]}\t{r["deny"]}')
        return 0
    if args.write or args.golden:
        target, content = (TABLE, table) if args.write else (GOLDEN, golden)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content.encode("utf-8"))
        print(f"wrote {target.relative_to(ROOT)} ({content.count(chr(10)) - (2 if args.write else 4)} rows)")
        return 0
    problems = 0
    for target, content, flag in ((TABLE, table, "--write"), (GOLDEN, golden, "--golden")):
        current = target.read_bytes().decode("utf-8").replace("\r\n", "\n") if target.exists() else ""
        if current != content:
            problems += 1
            want, have = content.split("\n"), current.split("\n")
            print(f"DRIFT: {target.relative_to(ROOT)} no longer matches process_command (regenerate with {flag} "
                  f"only if the routing change is intended)")
            for k in range(max(len(want), len(have))):
                a = have[k] if k < len(have) else "<missing>"
                b = want[k] if k < len(want) else "<missing>"
                if a != b:
                    print(f"  first difference at file line {k + 1}:\n    recorded: {a}\n    source:   {b}")
                    break
    if problems == 0:
        print(f"PASS: {len(routes)} serial routes and the unauthenticated golden file match process_command")
    return 1 if problems else 0


if __name__ == "__main__":
    sys.exit(main())
