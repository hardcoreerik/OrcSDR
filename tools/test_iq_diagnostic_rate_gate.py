"""Focused source contract: diagnostic raw-IQ recording must accept custom rates."""
from pathlib import Path
import re
source = (Path(__file__).resolve().parents[1] / "apps/orcsdr-tab5/ui/main.cpp").read_text()
pattern = r"if\s*\(([^{};]*g_iq_rec_kind\.load\(std::memory_order_relaxed\)\s*==\s*IqCaptureKind::diagnostic)\)\s*iq_rec_append\(block.data, block.bytes\);"
matches = re.findall(pattern, source)
assert len(matches) == 1, "Expected one raw diagnostic append condition"
assert "custom_rate" not in matches[0], "Diagnostic recorder rejects custom-rate IQ"
print("PASS diagnostic recording accepts default and custom-rate blocks")
