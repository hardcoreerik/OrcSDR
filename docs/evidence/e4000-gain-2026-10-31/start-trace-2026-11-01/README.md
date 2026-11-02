# Opt-in startup trace, 2026-11-01

Driver c4936da7c4261e2a64b65e56652df1b5e1bb8dd5, one approved build/flashCOM39.
CONFIG_ESP_RTL_SDR_E4000_START_TRACE=y, defaultn in driver. Managed source
compared with commit, image5,446,336bytes SHA256
3ecfea0db2bb4a63d958399f410f441457d602e4847c2ada9a8d5c5ab6f31e9c.
Esptool verified hashes, intentional reset. Previous a00e130 image retained.
COM17/COM8 untouched. Wider rebuild due new Kconfig; build passed.

Normal runtime harness11,390passed. Separate enabled host check compiled
actual snapshot function, confirmed E4000 reads/no tuner data writes, legacy
isolation and early read failure. That generated MSVC harness remains local
under driver tests/host/build/runtime-msvc/trace-enabled.cpp; not publicCI.

Same tinySA100.1MHz nominal-40dBm/30dB fixture, rate2.4MS/s, fixedmanual14.
RTL_SIGNAL means: initial-33.57, restart1-33.64, restart2-33.67dBFS.
Ten rounded/smoothed telemetry samples each,4ssettle,0.3s spacing. No rawIQ
captured in this traced run. Script exit0; sourceOFF/pause acknowledged.

Large ~1.85dB shift did NOT reproduce under instrumentation. All points were
near previous lower state. This is not a fix or proof that delay alone cures
it: reboot/initial state, extra repeater writes, register reads and logging
all differ. Do not infer an exact root cause from symptom suppression.

512 startup register lines retained at before_init/after_demod_init/
after_tuner_init/after_sample_rate/after_tune. Some startup/partial logs were
already buffered or consumed by authentication; snapshots are not complete
atomic dumps. Tuner gain fields and selected demod fields showed no new
obvious mismatch; tuner filters11/12 changed to initializationec/1f then
restored9a/07. DC calibration outputs2a/2b changed; these are output state,
not proof of wrong gain. Avoid speculative DC writes or gain-code changes.

Next discriminator should minimize observer effects: collect buffered snapshots
outside startup or isolate added timing from added reads, then repeat original
raw fixed-gain restart sequence. Current trace image is diagnostic, not release
qualified. Rawpriorresults remain authoritative for untraced symptom.
