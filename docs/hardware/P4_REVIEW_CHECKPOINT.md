# Scoped review checkpoint

These are implementation reviews, not release or hardware acceptance.
Grok received bounded source packets with tools and web access disabled.
Raw CLI logs stay in ignored `.local` files; do not publish account/tool logs.

| Scope | Findings verified locally | Disposition |
| --- | --- | --- |
| Luna Storage painter | Narrow label/value overlap; note clipped after wrapping | Fixed with shared text rectangles and measured note height; final Grok review CLEAN |
| Luna C6 API guard tests | Successful install confirmation untested; busy/phase/decision assertions missing | Expanded assertions using real API and shared atomic gate; Grok correction review CLEAN |
| Luna responsive text editor | Shared geometry and bounded redraw recording test | Grok found shifted-key field redraw missing; correction and regression underway; app viewport hookup remains |
| C6 worker | Shared timeout budget, reset while BOOT held after failure, ownership released before idle, Hosted OTA race | Corrected; further late-acknowledgment and fresh-generation corrections in review |
| Luna worker cleanup test | New actual-task fixture covers failure release and delayed cleanup | Two actual-task scenarios pass, including late acknowledgment after timeout; updated Grok review CLEAN |

Worker changes require an explicit BOOT-release acknowledgment after failure
before reset/Hosted resumption. They retain busy and update ownership until the
Wi-Fi owner acknowledges idle, use a separate version-check deadline, and share
an atomic ownership gate with Hosted OTA. Connection cancellation is cooperative:
it is handled after the bounded loader call returns, without killing a task
that owns SDIO. No worker candidate has been flashed or ROM-tested yet.

Focused commands run successfully after corrections:

```
python tests/test_c6_worker_guards.py -v
python tests/test_c6_worker_cleanup.py -v
python tests/test_p4_layout_contracts.py
```

The cleanup fixture runs the real task with hardware stubs, waits for the
explicit release acknowledgment, crosses the 40-second cleanup timeout with a
fake clock, and verifies busy/ROM ownership stay held until idle. Its flash
stub is never called. This does not establish real C6 ROM connection/write or
on-device Settings behavior.

Latest focused verification: guard suite 5 tests, cleanup suite 2 tests, layout
contract suite 4 tests, and text-editor recording suite 1 test all passed.
A prior cleanup review timing objection was checked against the fixture: it
records the first failure before advancing the fake clock and requires that
failure to occur after 40 seconds while ownership remains held. No production
change was made for that unsupported finding.

## r16 device checkpoint

The full LilyGO app build passed after correcting textWidth int32_t template
deduction and matching the recording stub signature. The app-only COM32 flash
passed esptool hash verification. Runtime reported Home at 96.1 MHz, connected
RTL-SDR streaming at about 2.4 MSps, and 29,080,484 bytes free PSRAM.
`tools/test_p4_recovery_guards.py --port COM32 --output <private-report>`
passed once startup completed: unauthenticated CONNECT and out-of-state INSTALL
were rejected, ownership remained idle, and writes stayed disabled. The first
check immediately after flash timed out before startup was ready; it did not
proceed to authenticated actions. No C6 ROM connection or write was performed.
Worker and editor Grok reviews remain pending. Physical screen/audio acceptance
of r16 remains separate from serial streaming evidence.

Landscape r16 manual rotation reported PASS at 1232x568 and native captures
show the waterfall refilling with RTL-SDR still streaming near 2.4 MSps.
However, comparison of the initial and settled native images shows contrast
arrows and stepper arrows/values missing after ordinary redraws. This is an
open rendering regression under focused investigation; landscape is not
accepted from these captures. Expanded worker cleanup suite now passes five
scenarios, including mocked install success, stale-generation rejection, and
cleanup reset failure; its new Grok review remains pending.

## Further review corrections

Grok verified a shifted-key editor field redraw omission. The correction now
redraws the field before the keyboard; the recording regression checks the
shifted character appears without a full clear. The editor correction review returned CLEAN.
Worker review found a cancellation acknowledgment race: pending cancel is now
cleared before publishing the failure-release prompt. Successful version
verification retains its matching sample rather than re-reading a later state.
The stale-version objection was checked against `wifi_service::start`: it
clears the cached version and queries the coprocessor synchronously before
returning success; the Wi-Fi owner advances generation only after that return.
This context was supplied to the correction review. The five cleanup and five
guard tests pass after these changes; this is not hardware write acceptance.

The next worker review exposed a second cancellation window during prompt
publication. Failure-phase release confirmation now atomically accepts either
no decision or a pending cancel, while refusing install in failure cleanup;
no intermediate clearing operation is needed. A guard regression covers
failure phase with decision=3. Five guard and five actual-task cleanup tests
pass; this source revision is not installed.

Radio Defaults and Companion compact geometry review returned CLEAN. Their
focused layout contract passes. System/clock geometry contract also passes,
with renderer/behavior review still pending. Compact navigation gates remain
restricted until all sections and their editors have migrated.

Human acceptance for installed r16: screen stable and FM audible in landscape;
user also reports the presentation looks good. This confirms those observed
properties, not all controls/dashboards or the separate captured redraw issue.

## r21 redraw review and recovery probe

Grok returned CLEAN for the bounded dirty-row tracker/presenter integration
and for the separate actual-present recording tests. All5 tracker tests and
3 presenter tests pass. User confirms the waterfall is more consistent.

With the external UART disconnected and the user holding C6 BOOT, the installed
worker quiesced Hosted and reached exclusive access2, but ROM preparation failed
with result107 (CONNECT_FAILED). The worker waited in release_boot_failure;
after the user confirmed physical release, RELEASED PROBE reset/resumed the C6
and returned access0/worker_busy0. Receiver streaming stayed near2.4MSps.
No flash operation was requested or performed. Stage-specific host/slot/loader/
connect logging has been added in source; that diagnostic revision is not yet
built/flashed. Hardware ROM connection and write acceptance remain open.

## r22 successful read-only ROM probe

With BOOT held and no UART adapter, host/slot/loader initialization and ROM
connect returned0, reset_failed0, phase release_boot. After physical release
and RELEASED PROBE, phase complete100/result0/access0/worker_busy0. No write
was enabled or requested. Subsequent Wi-Fi status started1/link_failed0/C6
2.12.9/stage none/code0 confirms Hosted resumed. Receiver remained~2.4MSps.
The earlier r21 connection failure is intermittent, not explained yet.

System review correction: shallow modal actions now fit even with flush
208pxcontent+48pxfooter (additional geometry case). Partial System clock-note
updates clear their note rectangle to remove stale glyphs. The alleged page
transition leftover was checked against draw_content, which clears all content
before page drawing; no extra broad repaint was added.

## Generic screenshot command

Canonical tool is tools/screenshot.py, Python callable screenshot(), and serial
command SCREENSHOT (case insensitive). FT8 SNAP remains a firmware alias and
the former tool module is a compatibility wrapper. Repository callers/current
documentation now use the generic name; historical changelog entries remain.
The P4 app build and flash passed with esptool hash verification. Running
`tools/screenshot.py --port COM32 --baud921600 <private-output>` captured a
55,044-byte PNG with verified transfer hash. Three focused screenshot tests
and26 affected FT8 device-tool tests pass. Tab5 source has the same command
alias through its existing authenticated capture path; Tab5 build/hardware
validation was not performed for this rename.

Connectivity compact migration now has3 recording tests (geometry/actions/
boundedupdates and the actual SSID-to-masked-password editor lifecycle) passing.
It remains behind the existing compact section gate until Location/DataMaps
and the adapter are ready. Grok Connectivity review is still running.

### 2026-11-01 Settings readability screenshot audit

- Built `.local/settings-text-audit-build.log` and flashed the application on COM32; esptool verified the written hash. C6 firmware was not changed.
- Increased responsive wrapped text to size 3 where the complete paragraph fits, retaining size 2 as the minimum. Display/Audio labels and values now use size 3 when space permits. Wi-Fi saved/network names occupy their own line; associated metadata remains separate. Form drawing and hit regions share the adjusted geometry.
- Captured 54 private native PNGs covering all 29 current Settings section/subpage combinations across portrait and landscape (duplicates cover single-page sections). Private evidence directory: `C:/Users/hardc/Documents/Codex/2026-10-30/i-x20/outputs/settings-page-audit-full`. Do not include these device snapshots in packages.
- Checked the portrait section overview and all landscape page captures, plus full-resolution Display/Audio, Firmware and Location views. Current displayed text fits; populated catalog/download/error states and editor screens still require screenshot coverage. An intermittent portrait capture loses header/button pixels after navigation; investigate before declaring visual acceptance complete. Do not conceal this with full-screen redraws.
- `python tests/test_settings_connectivity_recording.py -v`: 6 passed, including complete 32-character SSIDs and larger wrapping within reserved bounds. `python tests/test_settings_clock_recording.py`: 6 passed. `python tests/test_settings_c6_rom_recording.py`: 3 passed. These software checks do not replace physical visual acceptance or validate a C6 install.

### Settings redraw investigation and C6 repeat-tap guard

- Repeated native captures on Maps and Location were decoded back to RGB565 and compared with `RTL_SNAP_BUFFER raw_sha256`: all six matched the source buffer exactly (both byte orders checked). The missing portrait header is therefore in the UI buffer, rather than serial corruption or PNG pixel conversion. Header-loss root cause remains unproven; no full-screen redraw workaround was applied.
- Added a pending-action latch to ROM-SDIO Install/Resume/Cancel UI actions. Repeated taps return no action until the backend changes phase/busy state; enter/leave clears the latch. Beginning a new operation resets stale progress/result. Backend exclusivity and release acknowledgment remain unchanged.
- `python tests/test_settings_c6_rom_recording.py`: 3 passed, now including repeat Install/Resume/Cancel taps and refusal of Cancel immediately after Install is pending. `python tests/test_settings_connectivity_recording.py`: 6 passed.
- The repeat-tap guard is source-tested but not flashed yet. No physical C6 install was attempted. Grok correction review launched via `.local/review_corrections.py romui r2`; inspect its terminal result before claiming review acceptance.

### 2026-11-01 delegated dashboard migrations and trace replay

- User explicitly requested Luna-low delegation. Three non-overlapping agents own AM (then radio family), ACARS/POCSAG, and RF Lab respectively; parent retains shared architecture, Settings, serial/flashing and physical acceptance. Weather/Marine were found to use generic shared radio/Home paths rather than separate dashboard modules; do not create duplicate dashboards merely to add adapters.
- Temporary Settings chrome instrumentation is installed by `.local/settings-chrome-trace-build.log` / `.local/settings-chrome-trace-flash.log` (hash verified). It samples header-only graphics around touch and tick, capped at80 changes; no private text logged. A complete portrait replay of27 captures showed one initial header hash and no subsequent changes. Full-resolution Location page3 retained header graphics. This does not reproduce or explain earlier missing-header captures; root cause remains open. Remove temporary trace after diagnosis.
- Repeat-tap guard is now installed in this build. No C6 write attempted.
- Grok ROM-UI r2 completed with three MINOR observations: terminal phases while busy intentionally remain locked until backend cleanup; Begin allows cancel on next event intentionally because the adapter invokes begin synchronously before returning to event processing; version label remains hardcoded to3.0.6 and must follow package metadata if multiple embedded versions are supported (current package/worker are pinned3.0.6). No full-review or physical-install completion is implied.
- Corrected stale auto-rotation documentation: Settings controls exist, manual sample acceptance exists, FM/Settings have resize callbacks; physical axis calibration and automatic rotation acceptance are still pending.

### 2026-11-01 physical IMU axes and dashboard checkpoint review

- Six authenticated telemetry samples were captured in each human-confirmed pose. Mean g vectors: upright (0.825,0.004,-0.511); clockwise (0.021,-1.003,-0.063); inverted (-0.994,-0.039,-0.186); top-left (-0.065,0.973,-0.153). Private raw telemetry stays in local outputs. Profile axes now explicitly identify LilyGO's measured X/Y mapping; other profiles zero-initialize unverified. Auto remains opt-in. Source changes are not yet flashed or physically accepted for automatic rotation.
- `python tests/test_orientation_policy.py -v`: 6 passed, including replay of the four measured poses plus existing dwell/shake/gate/manual checks.
- Editor title/action labels choose size3 when the complete label fits, else2; `python tests/test_text_editor_layout.py` and `python tests/test_settings_connectivity_recording.py`: 1 and6 passed. Installed editor captures reproduced missing header at SSID-to-password transition; this remains an open redraw defect, not a verified fix. No credentials were submitted.
- Parent re-ran AM layout/update tests:2 passed. ACARS standalone compile initially failed missing <algorithm>; agent fixed it and supplied repeatable `python tests/test_acars_responsive_layout.py -v`:1 passed. RF Lab geometry compiled/passed manually; parent identified nontransactional canvas resize, returned it for correction. Combined IDF build awaits coherent held checkpoints; no dashboard hardware acceptance follows from these host tests.

### Combined dashboard checkpoint build

- Agents held edits for combined IDF build `.local/dashboard-checkpoint-build.log`. The build failed in `am_dashboard.cpp:786` with `-Werror=format-truncation` for the spectrum summary string. No new firmware was flashed. Parent returned the exact compiler error plus screen-width requirement to AM owner.
- RF Lab geometry, transactional allocation and Python suite entry are now present; agent reports5 layout contracts passed. Parent additionally requested compact tab-label text-width verification (MEASUREMENTS does not fit a ~97px portrait tab at size2) before device validation.
- ACARS host geometry test now passes under MSVC with warnings-as-errors. Its first checkpoint covers native-safe shell/LIVE paging only; detail and other tab bodies remain fixed-coordinate and unaccepted.
- AM source checkpoint covers all4views and overlays per agent, but current evidence is geometry plus source-contract checks, not a recording-display proof or physical acceptance. CB scout is ready for the next scoped migration after AM validation.

### Dashboard checkpoint r2 installed

- Combined build `.local/dashboard-checkpoint-r2-build.log`: PASS, app0x320a20 bytes (55% app partition free). Application flash `.local/dashboard-checkpoint-r2-flash.log`: esptool hash verified, normal reboot. C6 not written.
- Postboot native screenshot verified. RTL-SDR streaming2,400,028 effectiveSps; freePSRAM29,080,484 bytes, largest28,835,840; internalfree120,879; DMAfree81,291. This is a point observation, not long-run continuity acceptance.
- IMU reports available1 with measured profile. Authenticated AUTO ON succeeded, enabled1/auto_active1. Physical correct-direction question is pending; automatic rotation persistence/gesture/update-lock device acceptance remains open. Auto preference is currently ON for this test.
- Agent next tasks: CB implementation, ACARS detail migration, WiFiAnalysis read-only scout. AM/RFcheckpoint source compiled and installed; every-page pixel/touch acceptance still pending.

### Native AM/RF screenshots and auto-rotation direction rejection

- Captured16 hash-verified AM/RF Lab native PNGs (four tab attempts in both orientations), private outputs/dashboard-r2-pages. AM Settings tap remained on Spectrum, so this is NOT evidence of complete four-tab acceptance. AM titles are partly erased by dynamic updates; portrait preset/gain controls overlap; landscape Spectrum paints fragments below safe bounds. Returned findings and actual screenshots to AM owner for fixes and meaningful drawing regressions. AM entry also inherited24MHz/stopped; adapter restore range is under review.
- RF Lab settled Live successfully plots incoming IQ and populated metrics (`RF-LIVE-settled.png`); initial NO SOURCE was not a lasting runtime failure. Static Spectrum/Waterfall titles are erased by subsequent plot updates; returned for correction. Four RF page layouts fit sampled captures, but no full physical acceptance claim.
- Home restored FM96.1MHz streaming~2.4MSps after audit; C6 untouched.
- Human rejected first automatic landscape direction: rotates wrong way. Sensor pose identification was consistent, but view rotation needs opposite landscape angle. Corrected LilyGO profile policy Y=-sensorY; measured-pose test now expects clockwise270/top-left90, six orientation tests pass. Source correction NOT flashed yet. Disabled auto and applied manual270 on device; human confirmation of upright text is pending. Auto preference currently OFF for this recovery step.

### Restarted Luna agents and corrective checkpoint

- User requested restart after the three active Luna agents hit usage limits. Restarted the existing AM, ACARS and RF/RF24 task agents; all reported running and resumed existing owned files, without redoing completed work.
- AM corrective geometry now separates static titles/readout clears and Listen/Spectrum gain rectangles; compiled geometry/disjoint tests and source contracts pass4tests. Still no actual AM recording-display fixture. Follow-up Grok layout review launched as `.local/review_am_r2.py`; inspect live session77689/result before acceptance.
- Found serial-injected touch sequencing defect: if a second queued press starts immediately when the15-frame first press expires, no release event reaches touch callbacks. Changed injection start condition to require !was_down, guaranteeing a release iteration. Not flashed/device-verified yet; likely cause of captured Settings-tab nonselection.
- RF caption geometry test passes. RF24 layout+actual production renderer recording fixture pass2tests (`python tests/test_p4_layout_contracts.py -k rf24_dashboard -v`), covering portrait Devices/full32-charSSID and an ordinary paint update without a new full-screen clear. Five-page runtime capture remains pending.
- Human confirmed manual270 is upright with physical clockwise90. Corrected automatic Y sign remains source-only; device stays autoOFF/manual270 pending corrected build.

### Build-warning correction and review boundary

- Combined corrections build r3 (`.local/dashboard-corrections-r3-build.log`) failed in ACARS renderer at622/675, format-truncation warnings-as-errors. Returned exact fields to ACARS owner; use actual bounded row width and derived summary capacity rather than suppress diagnostics. Device still runs checkpointr2; no r3flash occurred.
- Parent confirmed AM4focusedtests, RF24layout+productionrecording2tests, RFcaptiongeometry1test, orientation6tests. ACARSAircraft hosttestpassed aftershadow-variablefix but hostgeometry/controller test does not compile M5 renderer, hence firmwarebuild remained necessary.
- Grok AMr2 (`.local/grok-am-responsive-r2.txt`, session77689) ended exit1 with only a statement of intended review, no findings or CLEAN verdict. Treat external AMr2review as incomplete, not accepted; don't infer approval from empty findings.

### Corrections r4 device evidence

- `.local/dashboard-corrections-r4-build.log`: PASS. `.local/dashboard-corrections-r4-flash.log`: esptool hash verified, app flashed normally, C6 unchanged.
- Captured16 new native PNGs in private outputs/dashboard-r4-pages: all4AM tab selections and all4RFpages in portrait/landscape. AMSettings now selects successfully (AM-90-3): physical event-delivery evidence supports release-frame injector correction.
- AMListen Frequency label, gain/preset separation visibly improved. Spectrum header at top still disappears; screenshot AM-0-2 returned toagent. AMstill24MHz despiteadapterrequest: globalhome_app.cpp kTuneMinHz24MHz/retuneclamp is authoritative cause, not savedstation restoration. Must audit actual dongle low-frequency/direct-sampling capability before changing radio limits; no reception acceptance implied.
- RFsettledLive now retains SPECTRUM/WATERFALL captions while actualplots/metrics update (RF_LAB-0-0), validating reserveddatarectfix for this sampledstate. OtherRFcontrols/pages captured, not allbehaviorphysicallyaccepted.
- Capture workflow temporarily disabled auto for repeatability and restored AUTO ON. Corrected sensorYsign is nowinstalled; humanclockwiserotationretstpending. No claim of persistentauto or gesture/update lock physicalacceptanceyet.

### P4 tuning policy correction (not yet installed)

- Traced AM's24MHz observation through current source: home_app::retune clamped allrequests tohardcoded24,000,000Hz before radio::tune_to. Public installeddriverheader defines ESP_RTL_SDR_FREQ_MIN_HZ24,000Hz / MAX1,766,000,000Hz and profilevalidation occurs in driverstart/retune; RTLprofilehelper alsoenforcesindividualranges. The UI minimum was stale, not proof the currentdongle lacksHF.
- Replaced Home's arithmetic clamp constants with publicdriverMIN/MAX macros (explicitesp_rtl_sdr.h include). This leaves driverprofilevalidation incharge and allows AMrequests toreach it. No managedcomponent/tuner/DSP change. Sourceonly; nextbuildmustverifyapplied1MHz and streaming, not merelyrequestedUItext. AMRFacceptance stillneedsappropriateantenna.
- Correctedauto-directionhumanquestion remains pending; avoid changingmanual/auto state repeatedly whilehumanmaybetesting.

### Storage integration and corrected header evidence

- Read-only installed `RTL_SD STATUS` reports attempted1/mounted1/total_mb29808. Settings adapter nevertheless hard-coded sd_ready=false. Replaced with real cached SD status/total/free capacity; free-space statvfs queries run in background SD task every5s, not UI. Serial status adds free_known/free_mb. Shared Settings distinguishes unknown free capacity from a full card and repaints only two value rectangles on status changes.
- `python tests/test_settings_connectivity_recording.py -v`:7passed, including actualrendered READY/FREE UNKNOWN/knownfree transitions with exactly2valuefills and nofullscreenclear. Clockrecording6passed before boundedstoragehelperaddition; firmwarebuild/flash/storagephysicalUIchecks pending.
- Correction to earlier AMheaderfinding: exactRGBheadercrop(72,72..496,128) SHA256 prefix81e27430deda77dd is IDENTICAL across all4r4portraitAMpagePNGs; brightpixelcount2575alsomatches. SavedPNGs do NOT show missingAMheader. Parentpreviewinspection was wrong; close r4AMheaderdefectwithoutredrawworkaround. Agentfoundno perframeScrollRectreset. OtherhistoricalSettingsheaderclaimsneedtheir own pixelcomparisons before treatingasbugs.

### Header investigation closed with exact evidence

- Exact header crop SHA256prefixa8be297bac2eeed6 matches SettingsDisplaypage, Locationpage3, SSIDeditor and passwordeditor nativePNGs. TogetherwithidenticalAMr4headercrops, the savedartifacts contradict the claimed missingheaderpixels. Close these specificfindingsthatcamefrompreviewinspection; do notretain themasdevicebugs oraddredrawworkarounds.
- Removedtemporarytrace_settings_chromefunction/callsfromhome_app.cpp; nootherHome trace removed. Sourcecleanupnotinstalledyet. `python tests/test_settings_connectivity_recording.py`:7passed.
- RemainingSettingsvalidation includes all editorstates/populatedlists and longmessages, separatefromthisclosedfalsefinding. Source/HWservice/UIacceptance boundariesunchanged.

### Auto-rotation clockwise physical acceptance

- Human answered Yes/correctnow to installedr4automatic uprightportrait -> physicalclockwise90 -> uprightlandscape test. This validates correctedYsignforoneautomatictransition; do notgeneralizetoallfourposes/rebootpersistence/gestureorfirmwarelocks.
- Auto remainsON aftertest. Manualfallbackpreviouslytested at270. Nextphysicalacceptance gates include opposite/invertedposes, gestureheldwhileturning, firmwarecriticalgate, and preservedpreferenceafterrestart.

### r6 installed and C6 completion display verified

- `.local/c6-completion-storage-r6-build.log` PASS and `.local/c6-completion-storage-r6-flash.log` hash verified. Postrestart native screenshot `C6-completed-screen-after-fix.png` shows C6 UP TO DATE / C6 3.0.6 / No update needed, larger completion guidance and no update button. This closes the requested completed-screen display correction for the sampled portrait state. Recording tests4pass cover completed/current plus recoverywhenunreachable.
- RTL-SDR stillstreaming~959844Sps onSettings/FM96.1MHz, PSRAMfree29064596, internalfree107859, DMAfree68271. C6workeridle afterP4restart is expected; currentversion is verified viaHostedtransport. C6notreflashedbyr6P4applicationflash.
- AllthreeLunaagentshitusageagain afterassignednexttasks; no repeatedrestartloop. Currentownededitsmustbeinspectedbeforeresuming. Parentcontinueslocaldevicechecks. AMactualappliedfrequency andstorageUIverificationpendingr6checks.

### r6 applied AM tuning and mounted Storage screen

- AuthenticatedUIopenAM thenread-onlyRTL_STATUS reportsconnected1/streaming1/effective_sps959793/frequency_hz1000000. This verifiesdriverapplied1MHz afterremovingstale24MHzUIclamp. PrivatenativePNG r6-AM-tuned.pngcaptured. NoAMreception/audioqualityclaimwithoutantenna/physicalacceptance.
- RTL_SD_STATUS reportsattempted1/mounted1/total_mb29808/free_known1/free_mb27589. SettingsStorageprivatePNG r6-Storage-mounted.pngshowsREADYand26.9/29.1GBFREE, readablewithinsafearea. Thisvalidatesrealcachedstatusbridge; database/map/logusageactionsstillplaceholderandnotcompleted.

### r6 ACARS/ADS-B/Wi-Fi native page audit

- Captured32 nativehash-verifiedPNGs:6ACARStabs+5ADS-Bviews+5WiFiAnalysispages inbothorientations, privateoutputs/r6-dashboard-audit. ACARSoriginalscriptcount7wasstopped/revisedtoregistry6beforefinalaudit; finalfilenamescover0..5. Thesearepagecapturesnotallpopulateddata/editorstates.
- Foundactualtextdefects: ACARSLivestatus centeredonst.x+8ratherthancardcenter, causingoutsidecardtext; ADS-Bradarcoordinate/rangesummarytoo wideforportrait. FixedACARScenteranchorandportraitsplitinto2lines. ADS-Bresponsivecaptionnowputsrangeinthetitleandfullcoordinatepaironseparateline,legacyformatretained. No fontsizedecrease or redrawexpansion. HosttestsACARS1andADSBlayout1pass; sourcefixesnotyetbuilt/flashed/pixelaccepted.
- AMapplied1MHzandStorageREADY/freecapacityverifiedearlierthischeckpoint. Remainingdashboardcaptureanalysis, crossprofiletests, unportedfamilies andhardwareacceptance remainopen; do notmarkentireportcomplete.

### Native audit follow-up and interrupted CB compile

- r7firmwarebuildterminalfailed CBdraw_spectrum_static usingundefinedViewportClip, frompartiallypersistedagenteditbeforeusagefailure. AddednarrowlocalRAIIcliphelper; noCBservice/scannerchanges. CB2focusedtests pass, fullfamily stillpartial.
- WiFiAnalysisnativeCSI/Settingscaptures showedtextincompleteness. Rootcause draw_wrapped scratchline34returnedwithoutpaintingremainingtextiflinecapacityreachedbeforepixelwidth (especiallylandscape). Enlargedscratchto256forcurrentboundedparagraphs andaddedactualproductionrendererrecordingcheck reconstructingentire75charparagraph acrossbothorientations. TestinitiallycaughtADLambiguousset_viewportincall; explicitlyqualifiedrf24setter. RF24layout+recording2testsnowpass. CSI/Settingscopyshortenedtouserfacingcompleteguidanceandflowedfromactualrenderedlinecount.
- r8buildlaunched `.local/dashboard-text-r8-build.log` session10073; noflashyet. ACARSLivestatuscenter/twolinefixandADSBrange/coordinatecaptionfixincluded, awaitingnativepixelchecks.

### r8 text corrections installed and checked

- `.local/dashboard-text-r8-build.log` PASS; app0x32d480,55%partitionfree. `.local/dashboard-text-r8-flash.log` esptoolhashverified. C6unchanged.
- NativeportraitACARSLivecardnowcenterscomplete2linestatusinsidecard;ADS-Bradartitle/rangeandcoordinatepairfitinsideportraitpanel. Capturesprivateoutputs/r8-text-recheck/ACARS-0-0.png andADSB-0-0.png.
- Initialr8batchWiFifilenamescontainedHome/gallery: OPENreplywasnotvalidatedandmutationauthenticationcouldexpire. Exclude thoseWiFiimagesfromacceptance. CorrectedcapturehelperauthenticatesmutationsandrequiresRTL_UI_OPEN_OK. Repeatedall5WiFipagesbothanglesinprivateoutputs/r8-wifi-verified. CSIandSettingsparagraphsrendercompletely, albeitcharacterwrappingcanbreakwords; improvewordbreakreadabilitynext. Screenshotrecheckdoesnotestablishallscanstates/populatedlistsortouchacceptance.
- RF24layout+actualrendererrecording2tests pass (includeswholeparagraphreconstructionbothwidths);CB2contracts,C6recording4,orientation6passed. CBpartialSPECTRUMworkstillneedscompletedrenderer+touchparity,remainingfamiliesandcrossprofileacceptanceopen.

### Shared word wrapping and CB Spectrum completion slice

- Replaced RF24character-onlywrappingwithspace-boundarywrappingandboundedlong-tokenprogress. CompleteSSIDtokensretained; paragraphscontinuebeyondold33charlimit. ActualproductionrendererrecordingtestcheckswholeparagraphcontentbothwidthsandConnectivitywordremainsononeline. `python tests/test_p4_layout_contracts.py -k rf24_dashboard -v`:2passed.
- CompletedinterruptedCB SpectrumgeometrywiringforliveFFT/canvas,waterfallpush,frequencytoXmapping,andtap-to-channel. Allconsumeexistingnativeplot/waterfallrectangleswhilelegacydefaultremains. Scratcharraysstayfixed1232columns, nativewidthguardpreventsoverrun. No scanner/DSPalgorithmchange. Addedgeometryassertionsforplot/waterfall/axisboundsanddisjointness. CB2tests pass; fullrendererrecordingfixture stillmissing, Activity/Channels/Setupstillincomplete.
- Firmwarebuild `.local/cb-spectrum-wrap-r9-build.log` session76523waslinkingwithnoerroratlastobservation; notyetflashed. Inspectterminalresultbeforedevicework.

- r9buildterminalPASS. r9flashlaunchedwithesptoolapp-onlyCOM32; verify `.local/cb-spectrum-wrap-r9-flash.log` hash/terminalbeforeclaiminginstalled. NoC6write.

### r9 physical CB/Wi-Fi checks

- r9flashhashverifiedterminal0. CB SpectrumprivatePNGs capturedbothangles showliveFFT/waterfallinsidecomputednativeplotregions. PortraitretainedlastListenchanneltilesbelowwaterfall; rootcause tab_touch redrewonlynewframewithoutclearingoldcontent. Sourcefixnowclearscontentrectangleonlyonactualtabchange, preservingheader/nav andreturningwithoutredrawforcurrenttabtap. Axislabelsderiveactualnearestchannelinsteadlasttick41andclamptextanchorsinsideaxis. CB2tests pass; thesefinalcorrectionsnotyetflashed.
- WiFiCSIportraitwordwrap recheckedwithcorrect1128second-rowtabcoordinate, privateWiFi-CSI-portrait-word-wrap.png. Earlierhelperused1100inthegap; excludeearlierCSIportraitfilenameunlessactualpageverified. Auto restoredONandHomeaftercapture.

### CB Activity native layout slice

- Added responsive Activity rendering with7portraittwo-linerows/4landscapecompactrows, sharedrowrectanglesforpaint/touch, activechannelcount, PREV/NEXT/CLEAR andclampedpageoffset. Existing scanner/logdata andlegacy7-rowviewpreserved. Fixedlandscapesummary/firstrowoverlapbyreserving24pxsummarybandandstartingrowsbelowit.
- NativegeometrytestassertsActivityrowsinsideviewport,>=44pxanddisjointfromsummary/neighbors;CB2tests pass. Actualpopulatedlogrender/inputdeviceacceptanceandrecordingfixturestillopen. CurrentActivityupdatesrepaintownedcontentbutnotheader/nav; narrowerperrowdiffcanbefollowupafterfunctionalvalidation.
- `.local/cb-activity-r10-build.log` session12278running; includespriorCBtabtransitioncleanupandaxislabelcorrection. Do notclaimflasheduntilesptoolhash/afterPNGsverified. CBChannels/Setupstilllegacyandunfinished; fullportgoalunchanged.

### r10 Activity installed; Channels source migration

- r10buildPASSandappflashhashverified. Capturehelper `.local/capture_r10_activity.py` session62464capturesActivitybothangles; inspectPNGs beforeclaimingUIacceptance.
- Whileflashusedalreadybuiltbinary, completedCBChannelsresponsivebranchinsource: sharednativegridcellsfeedpaint/touch, channel/frequency/lock-hit labels size2, portraitstackedmetadata/landscapetwo-linecells, SCANALL/SSBactionsfromsharedrectangles. Legacypathretained; no scannerstatechange. CB2tests pass; ChannelssourceNOTinr10binary,stillneedsrendererbuild/physicalvalidation. Setupremainsunported.

### CB Setup native migration checkpoint

- AddedsharedresponsiveSetupdraw/touchfor2pages×4rows usingexistinglayoutrectangles, font2helpwrappedatwords/font3portraitlabels, cyclecontrolsand +/-actionsretained. Channelshitcountsnowshowfulluint16countalonewithsharedsubtitlelegend,avoiding65535HITSoverflow;testscheck65535/27.405/LOCKEDfitandSetuphelp/action/pagerdisjointness. CB2focusedtestspass.
- r11firmwarecompilecaughtduplicateg_setup_pagedeclarationintroducedwhileconnectingexistingdraft;removedduplicate. Correctedbuild `.local/cb-channels-setup-r11b-build.log` session81582running. NoSetup/Channelsflashyet. Rendererrecording/populatedlogtestsremainopen; sourcecontractsarenotpixelproof.

## 2026-11-01 branch checkpoint
- CB Channels landscape screenshot exposed the final grid row overlapping tabs. Shared geometry now puts the heading and hint on one line and keeps all 40 cells within content; regression failed before the fix and passes afterward.
- Validation: python -m pytest tests/test_cb_dashboard_layout.py -q: 2 passed. Prior r11b app build and flash hash verified; this final geometry correction has not yet been rebuilt or physically checked.
- This is an integration checkpoint, not completion or release acceptance. Private device screenshots remain outside version control.
