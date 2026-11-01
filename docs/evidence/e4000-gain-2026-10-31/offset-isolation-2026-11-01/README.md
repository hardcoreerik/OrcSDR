# XTR offset isolation, 2026-11-01

Same firmware, COM39, CW 100.1 MHz nominal -40 dBm through 30 dB attenuator.
Receiver 100 MHz/2.4 MS/s. No flash or tablet restart command. Each capture
stops reception for retrieval and restarts for the next point; this is not
an uninterrupted live-gain test. The recorded RTL_OFFSET_STATUS confirms
requested/applied state. Manual gain state was checked before capture.

Integrated tone dBFS, same +/-4-bin Hann method as focused repeat:

| Offset | Gain dB | Tone dBFS |
| --- | ---: | ---: |
| OFF | 14 | -24.557 |
| OFF | 16.5 | -25.203 |
| ON | 14 | -27.487 |
| ON | 16.5 | -26.332 |
| ON | 14 | -27.516 |
| OFF | 14 | -26.477 |

Seven captures total, 33,600,000 bytes, matched device SHA-256; six source-on
captures passed tone/rail checks with zero rail clipping. Script completed;
output OFF and pause acknowledged. Final offset restored ON and gain -1 dB.

The first OFF pair reproduces a -0.645 dB dip, whereas the ON pair rises
1.155 dB. Two ON 14 dB results agree within 0.030 dB. First/last OFF 14 dB
results differ -1.919 dB. Offset state alone does not explain repeatability;
start/stop, tuner state and source conditions are not fully isolated.
No root cause or fix is claimed. Raw CU8 remains in ignored build directory.
Archived script retains actual local paths; repeat using parent fixture
instructions and a new output directory. Events preserve serial/source state.
