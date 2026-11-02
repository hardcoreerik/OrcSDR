# Timing-only startup control, 2026-11-01

Driver25b0251763b2cf46fffc67f7540dfdf50e499b9e, trace+delay-only enabled.
Five110ms waits replace startup snapshot reads/repeater writes. DefaultsOFF.
Measured read snapshot spans~97-98ms; wait110ms approximates transaction overhead,
not exact timing equivalence or production settling recommendation.
Host normal11,390checks passed; timing-only generatedhostcheck confirms zero
traceUSBtransfers,110msE4000wait,legacyunaffected. One approvedbuild/COM39flash,
5,445,376bytes SHA256d394ffe52b3de5babe8dc9bce11e22ef923f4697883e60b8c021a836e15ccb08.
Esptoolhashverified; intentional reboot; previousreadtraceimage retained.
COM17/COM8 unchanged. WiderKconfigrebuild passed.

SameXTR/tinySA100.1MHz nominal-40dBm/30dB fixture, receiver100MHz/2.4MS/s.
Initial telemetry attempt timed out driverSTATUS beforeRFON, OFF/pauseconfirmed.
Standalone query succeeded. Settledtelemetry added3safterqueuedtune; readings
initial-33.54,restart1-33.57,restart2-33.60dBFS. Smoothing, timing and initial
state differ from rawrunner; this alone did not prove symptom suppressed.

Firstrawattempt excluded: userreportedaccidentalscreentouch and IQhashfailed.
No causal attribution between them. RFoutputOFF/pauseacknowledged, runnerended,
retrievalaborted/released. PosttouchSTATEstreaming100MHz/manual14, counters0/0.
Invalidatedattempt and partialraw preserved ignored builddir.

Cleanrawrepeatcompletedexit0, sourceoffbaseline+fixed14before/afterone restart.
Three4.8MBCU8files independentlySHA256verified,no railclipping. Streaming
continues during retrieval; gainunchanged between signalpoints, sourceconstant.
IntegratedHann+/-4bin tone: before-27.6331069,after-29.4951948dBFS.
Restartdelta=-1.8620879dB. Thus timing-only control did NOT fix shift.
OFF/pauseacknowledged at completion, receiver restoredmanual-1dB.

Do not introduce unconditional sleeps as an accepted fix. Stable telemetry
in this experiment and earlier readtrace is not equivalent to rawacceptance.
Stillcompare extra read/repeater actions or broader register state using
matchedinitialconditions; exactrootcauseunconfirmed. Driver remainsdiagnostic,
not releasequalified. Archivedscripts retainlocalpaths; choosefreshoutputdirs.
RawIQ/binaries remainignored; JSON contains hashes. No source secrets archived.
