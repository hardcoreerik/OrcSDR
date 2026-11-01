# XTR PC gain repeat, 2026-11-01

XTR USB serial 82115932 verified on PC. Same tinySA RF -> 30 dB attenuator
fixture confirmed by user. CW 100.1 MHz nominal -40 dBm; rate 2.4 MS/s,
receiver 100 MHz, tuner manual, RTL digital AGC off, bandwidth auto.
Reference DLL C:/Tools/esp-rtl-sdr-lab/v4l-driver-1.4.0/x64/rtlsdr.dll.
This is the existing PC reference binary, not the ESP driver or a new
manufacturer-driver qualification. No DLL source imported.

Sequence 14/16.5/19/16.5/14/29/34/29 dB, offset OFF/ON/OFF. All 24 signal
captures passed frequency/rail checks and were independently rehashed.
Two baseline captures additionally retained locally. Script and USB capture
exited 0. Source output OFF and pause acknowledged twice at completion.
USB capture bus1/device10 decoded 846 control transfers, 0 unpaired submits.
Decoder updated only to annotate E4000 address 0xc8; original decoder labels
only 0x34/0x74. No source/control semantics changed by this annotation.

Integrated +/-4-bin Hann power (same convention as ESP repeat) shows stable
same-gain returns across all three sequences; exact deltas are reproducible
from integrated-results.json. The PC 14->16.5 step rises approximately 1.18 dB;
29/34 plateau remains. Offset changes absolute tone level but does not
reproduce the ESP 14 dB repeat shift. Combined with stable V5 control,
this prioritizes E4000-specific ESP state/sequencing investigation. It does
not identify the exact root cause or eliminate every instrument variable.

USB writes confirm LNA codes 08/09/0a/09/08/0d/0e/0d. PC initialization
also writes tuner IF registers 16 through 49/41/01 and 17 through 02/12,
then manual mode 1a=50 and 20=00. Those initialization differences need
comparison with actual ESP register state; do not adopt writes blindly.

No tablet or firmware changed in this PC run. Raw CU8 and pcap remain local
in the PC capture directory; JSON hashes identify IQ. Archived scripts retain
actual paths; choose fresh output directory and confirm hardware identity
before repeating. Follow parent fixture instructions. No driver fix claimed.
