"""Stage Espressif's error-before-halt fix without changing the shared SDK.
Usage: python tools/prepare_usb_hal_lab_override.py SDK_ROOT APP_PROJECT
Refuses existing overrides. Run only for an approved lab firmware build.
"""
import hashlib, json, shutil, sys
from pathlib import Path
sdk, project = [Path(x).resolve() for x in sys.argv[1:3]]
source = sdk / "components/hal"
dest = project / "components/hal"
assert (project / "CMakeLists.txt").is_file(), "Not an application project"
assert source.is_dir() and not dest.exists(), "Missing SDK HAL or existing override"
original = (source / "usb_dwc_hal.c").read_bytes()
line = b"        HAL_ASSERT(chan_intrs & USB_DWC_LL_INTR_CHAN_CHHLTD);  //An error should have halted the channel"
assert original.count(line) == 1, "SDK differs; review before adapting"
newline = b"\r\n" if line+b"\r\n" in original else b"\n"
patched = original.replace(line+newline, b"")
assert patched != original
shutil.copytree(source, dest)
(dest / "usb_dwc_hal.c").write_bytes(patched)
changed = []
files = 0
for p in source.rglob("*"):
    if p.is_file():
        files += 1
        if p.read_bytes() != (dest/p.relative_to(source)).read_bytes():
            changed.append(str(p.relative_to(source)))
assert changed == ["usb_dwc_hal.c"], changed
assert (source/"usb_dwc_hal.c").read_bytes() == original
meta = dict(upstream_commit="63b468539f86f759ae1075ed277b2da65eccbdfe", source=str(source), override=str(dest), files_verified=files, changed_files=changed, before_sha256=hashlib.sha256(original).hexdigest(), after_sha256=hashlib.sha256(patched).hexdigest(), shared_sdk_unchanged=True)
(dest.parent / "hal-lab-provenance.json").write_text(json.dumps(meta, indent=2)+"\n")
print("Prepared local HAL override; verify CMake selects it before flashing")
