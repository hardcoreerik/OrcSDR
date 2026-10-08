# JS8 fixture sources

No owner RF fixtures are committed yet.

Future entries must identify:
- fixture ID;
- raw-IQ SHA-256;
- derived 12 kHz WAV SHA-256;
- UTC;
- band/dial frequency;
- sample rate and format;
- RTL-SDR.com V4;
- MLA-30+ antenna;
- gain setting;
- reference decoder/version and decoded text;
- derivation vs withheld-verification assignment.

The public API tone vector used by `js8_frame.cpp::self_check_frame` is documentation data, not an RF recording:
https://js8call.com/JS8Call-improved/d7/d15/md_docs_2API.html
