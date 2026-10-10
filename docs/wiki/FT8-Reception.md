# FT8 Reception

OrcSDR’s FT8 implementation is receive-only: the waterfall shows signal activity and the decoder turns valid messages into rows. A bright line is signal energy; it is not itself a successful decode.

This page covers mainline FT8 support and the dated test below. Older wiki screenshots and beta.1 package descriptions do not establish FT8 availability in that package.

## Listen on an active band

1. Connect a supported receiver and an antenna suitable for the band. Let the normal startup checks complete and close the splash screen, or let it time out.
2. Open FT8 RX from the dashboard menu. Choose FT8 and a band preset; opening the dashboard alone does not select a new frequency.
3. For 40 metres, the conventional FT8 dial frequency is **7.074 MHz**. Choose the band for current propagation and your antenna; activity varies with location and time.
4. Check that the UTC clock is correct. FT8 uses 15-second slots; allow several complete slots after tuning before judging reception.
5. Watch the decodes as well as the waterfall. A busy waterfall with no messages can mean weak signals, clipping, timing trouble or an antenna connection problem.

The dashboard offers LIVE, DECODES, MAP, HUNTER, HEARD and SETUP. Hunter can look for activity, but energy and a valid decoded message are different evidence. Map positions come from station-reported locators, not measured transmitter locations. Core reception does not require Internet access; a correct clock still matters. PSK Reporter can help choose an active band, but its reports do not prove reception on your device.

## Gain and antenna checks

Use the GAIN panel and observe clipping. Automatic gain can overload on a strong signal; try manual gain and reduce it when clipping rises. There is no one gain value for every receiver or band. Direct-sampling HF bypasses tuner gain, so changing tuner gain need not change the HF signal level.

Check antenna connectors before assuming a receiver fault. An externally powered **MLA-30+ does not need the dongle bias tee**; leave Bias-T off for that setup.

## Recorded reception check — 9 October 2026

The owner accepted FT8 reception on **Blog V4, Blog V4L, V3c and Nooelec NESDR SMArt V5**, using an externally powered MLA-30+ in Springfield, Oregon, on 40 metres at 7.074 MHz. The session started on source `52cd8bd`; later driver checks used a local build with diagnostic self-check changes. This is a bounded reception check, not a comparison of sensitivity or a promise for every antenna, band or firmware build.

V4 and V3c produced decodes after reboot; V4L also produced decodes. Nooelec reception improved and decoded after tightening the antenna connection. Automatic gain clipped during the V4 test; manual gain was used. These observations do not establish a general hot-swap fault. Wi-Fi and OrcDial reconnection were also confirmed by the owner after a later power reboot.

Screenshots will be added after a separate capture session.
