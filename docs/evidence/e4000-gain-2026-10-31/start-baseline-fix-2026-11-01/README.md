# Startup gain-history correction, 2026-11-01

Discriminator on existing timing-control image: initial14=-31.9; restart14=-33.6;
live-1->14=-33.6; retuneat-1then14=-33.6; restartat-1thenlive14=-31.97dBFS.
Thus startup gain history changes resulting signal level. Exact analog mechanism
is not proven; earlier assumptions that gain-field writes were failing were wrong.

Driver a3344bb establishes fixed manual-1 LNA/mixer/IF baseline at end of E4000
initialize without changing requested mode/gain shadow. Tune restores request
before IQ. No gain ladder change or added delay. Host runtime12,177checks passed,
including inheritedhighgain->lowbaseline, requested14 preservation/restoration.

Approved build/COM39flash passed; provenance.json records image8a43bbde3fb965903f60f9d28dc185c9b63dd4e1bcfb9c62c7222b49e734ecfa,
5,444,944bytes. Managed source compared; trace/delay flagsdisabled. Esptoolhash
verified; intentional reboot. COM17/COM8 unchanged; previousimage retained.

Samefixture tinySA100.1MHz nominal-40dBm through30dBattenuator, XTRCOM39,
receiver100MHz/2.4MS/s. Fresh fixed14 before/afterrestart rawpair completedexit0;
no gaincommand between points. Both4.8MB files matched deviceSHA256,no rails.
IntegratedHann+/-4bin tone before=-27.4801266,after=-27.5679913dBFS.
Delta=-0.0878647dB versus prior-1.8539/-1.8621dB. This candidate passes this
specific regression pair. Only one RFpair, onefrequency/rate/device/fixture;
not all-band calibration, vendorparity or fullrelease qualification.
FinalsourceOFF/pauseacknowledged. RawIQ retained ignored builddirectory.

Initial telemetry and rawattempts encountered status/result response timeouts;
first rawbaseline preserved, not acceptance. Cleanrunner retries readonlySTATUS
queries only, never blindly retriestune/gain. Subsequent14-step/AUTO serialsmoke
also timed out after accepting gain15; restore response timed out too. Archived
serial-smoke-incomplete.json is a FAIL/incomplete check, not a pass. Console
reliability and broadercontrols remain open. Do not merge/tag on this result alone.
