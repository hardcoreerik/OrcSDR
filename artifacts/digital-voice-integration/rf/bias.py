import sys
sys.path.insert(0, "F:/Ai/OrcSDR/.claude/worktrees/dmr-digital-voice-review-dde01d/tools")
from help_media import Tab5
from pathlib import Path
c = Tab5("COM17", Path("F:/Ai/OrcSDR/.orclink/ui-doc.key"))
try:
    c.authenticate(create_key=False)
    for cmd in sys.argv[1:]:
        c.send(cmd)
        try: print(cmd, "->", c.wait(("RTL_DRIVER","RTL_DV"), 8))
        except TimeoutError: print(cmd, "-> timeout")
finally:
    c.close()
