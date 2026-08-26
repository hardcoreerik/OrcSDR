#include "rf_visualizer_controls.hpp"

#include <cstring>

namespace orcsdr::visualizer {
namespace {

#define TOG(v, g, id, label, d) {v, g, ControlKind::toggle, id, label, 0, 1, 1, d, "Off|On", cap_none, false, ""}
#define INT(v, g, id, label, lo, hi, step, d) {v, g, ControlKind::integer, id, label, lo, hi, step, d, "", cap_none, false, ""}
#define REAL(v, g, id, label, lo, hi, step, d) {v, g, ControlKind::real, id, label, lo, hi, step, d, "", cap_none, false, ""}
#define CH(v, g, id, label, choices, d) {v, g, ControlKind::choice, id, label, 0, 31, 1, d, choices, cap_none, false, ""}
#define ACT(v, id, label) {v, ControlGroup::action, ControlKind::action, id, label, 0, 0, 0, 0, "", cap_none, true, ""}
#define CAP(v, g, kind, id, label, lo, hi, step, d, choices, caps, commit, why) {v, g, kind, id, label, lo, hi, step, d, choices, caps, commit, why}

constexpr ControlDescriptor kControls[] = {
    CAP(View::common, ControlGroup::quick, ControlKind::real, "rf.center_hz", "Center", 100000, 1800000000.0f, 1000, 100000000, "", cap_iq, true, "Start an RF source first"),
    CAP(View::common, ControlGroup::quick, ControlKind::real, "rf.span_hz", "Span", 1000, 2400000, 1000, 960000, "", cap_iq, true, "Start an RF source first"),
    CH(View::common, ControlGroup::advanced, "rf.gain_mode", "Gain", "Manual|Tuner AGC|RTL AGC", 1),
    REAL(View::common, ControlGroup::advanced, "rf.gain_db", "RF Gain", -10, 50, 0.1f, 0),
    REAL(View::common, ControlGroup::display, "display.floor_dbfs", "Floor", -140, -20, 1, -110),
    REAL(View::common, ControlGroup::display, "display.ceiling_dbfs", "Ceiling", -100, 10, 1, -20),
    CH(View::common, ControlGroup::display, "display.auto_levels", "Auto Levels", "Off|Slow|Fast", 1),
    CH(View::common, ControlGroup::display, "display.grid", "Grid", "Off|Major|Major + Minor", 1),
    CH(View::common, ControlGroup::display, "display.palette", "Palette", "Classic|Turbo|Viridis|Inferno|Grayscale|Amber|Green|Heat", 0),
    TOG(View::common, ControlGroup::quick, "visual.freeze", "Freeze", 0),
    CH(View::common, ControlGroup::display, "visual.quality", "Quality", "Max|Balanced|Radio Priority", 1),
    CH(View::common, ControlGroup::display, "visual.fps_overlay", "Performance", "Off|Compact|Detailed", 0),
    ACT(View::common, "visual.reset", "Reset View"),

    CH(View::spectrum, ControlGroup::quick, "fft.size", "FFT Size", "256|512|1024|2048|4096", 2),
    CH(View::spectrum, ControlGroup::advanced, "fft.window", "Window", "Rectangular|Hann|Hamming|Blackman-Harris|Flat Top", 1),
    CH(View::spectrum, ControlGroup::advanced, "fft.overlap", "Overlap", "0%|25%|50%|75%", 2),
    CH(View::spectrum, ControlGroup::advanced, "fft.detector", "Pixel Detector", "Max|RMS|Average", 0),
    CH(View::spectrum, ControlGroup::quick, "fft.average_mode", "Averaging", "Off|EMA|Linear Power", 1),
    INT(View::spectrum, ControlGroup::advanced, "fft.average_time_ms", "Average Time", 50, 5000, 50, 500),
    CH(View::spectrum, ControlGroup::quick, "fft.peak_hold_mode", "Peak Hold", "Off|Infinite|Timed|Decay", 3),
    REAL(View::spectrum, ControlGroup::advanced, "fft.peak_hold_s", "Hold Time", 1, 60, 1, 10),
    REAL(View::spectrum, ControlGroup::advanced, "fft.peak_decay_db_s", "Peak Decay", 0.5f, 30, 0.5f, 3),
    INT(View::spectrum, ControlGroup::quick, "fft.marker_count", "Peak Markers", 0, 8, 1, 6),
    REAL(View::spectrum, ControlGroup::advanced, "fft.marker_threshold_db", "Marker Threshold", 3, 30, 1, 10),
    INT(View::spectrum, ControlGroup::display, "fft.trace_thickness", "Trace Width", 1, 3, 1, 1),
    CH(View::spectrum, ControlGroup::display, "fft.center_cursor", "Center Cursor", "Off|Line|Line + Readout", 1),
    ACT(View::spectrum, "fft.clear_peak", "Clear Peak Hold"),

    CH(View::waterfall, ControlGroup::quick, "waterfall.palette", "Palette", "Classic|Turbo|Viridis|Inferno|Grayscale|Amber", 0),
    CH(View::waterfall, ControlGroup::advanced, "waterfall.direction", "Direction", "Newest at Top|Newest at Bottom", 0),
    CH(View::waterfall, ControlGroup::quick, "waterfall.speed", "Rows / Second", "Auto|5|10|15|20|30|45|60", 0),
    INT(View::waterfall, ControlGroup::advanced, "waterfall.history_s", "History Buffer", 10, 120, 5, 30),
    REAL(View::waterfall, ControlGroup::advanced, "waterfall.gamma", "Contrast Curve", 0.4f, 2.5f, 0.05f, 1),
    CH(View::waterfall, ControlGroup::advanced, "waterfall.level_mode", "Level Source", "Inherit Common|Custom", 0),
    REAL(View::waterfall, ControlGroup::advanced, "waterfall.floor_dbfs", "Waterfall Floor", -140, -20, 1, -110),
    REAL(View::waterfall, ControlGroup::advanced, "waterfall.ceiling_dbfs", "Waterfall Ceiling", -100, 10, 1, -30),
    CH(View::waterfall, ControlGroup::advanced, "waterfall.bin_mapping", "Horizontal Mapping", "Max Bin|Average Bin|Linear Interpolation", 0),
    CH(View::waterfall, ControlGroup::display, "waterfall.time_grid_s", "Time Grid", "Off|1|5|10|30", 2),
    CH(View::waterfall, ControlGroup::display, "waterfall.frequency_labels", "Frequency Labels", "Off|Edges|Full Grid", 2),
    ACT(View::waterfall, "waterfall.clear", "Clear History"),

    REAL(View::phosphor, ControlGroup::quick, "persistence.half_life_s", "Persistence", 0.1f, 30, 0.1f, 3),
    CH(View::phosphor, ControlGroup::quick, "persistence.accumulation", "Accumulation", "Maximum|Additive|Histogram Density", 2),
    REAL(View::phosphor, ControlGroup::quick, "persistence.exposure", "Exposure", 0.25f, 8, 0.25f, 1),
    CH(View::phosphor, ControlGroup::advanced, "persistence.decay_curve", "Decay", "Linear|Exponential", 1),
    INT(View::phosphor, ControlGroup::advanced, "persistence.point_size", "Point Size", 1, 3, 1, 1),
    INT(View::phosphor, ControlGroup::advanced, "persistence.blur_px", "Phosphor Spread", 0, 2, 1, 0),
    CH(View::phosphor, ControlGroup::quick, "persistence.palette", "Phosphor Color", "Green|Amber|Cyan|Heat", 0),
    CH(View::phosphor, ControlGroup::advanced, "persistence.rare_hold_s", "Rare Event Hold", "Off|2|5|10|30", 0),
    REAL(View::phosphor, ControlGroup::advanced, "persistence.background_reject_db", "Noise Rejection", 0, 20, 1, 3),
    ACT(View::phosphor, "persistence.clear", "Clear Phosphor"),

    CH(View::spectrum3d, ControlGroup::quick, "spectrum3d.slices", "History Slices", "16|24|32|48|64|96|128", 4),
    REAL(View::spectrum3d, ControlGroup::advanced, "spectrum3d.history_s", "History Time", 2, 60, 1, 20),
    REAL(View::spectrum3d, ControlGroup::quick, "spectrum3d.elevation_deg", "Elevation", 10, 70, 1, 32),
    REAL(View::spectrum3d, ControlGroup::advanced, "spectrum3d.azimuth_deg", "Azimuth", -45, 45, 1, 20),
    REAL(View::spectrum3d, ControlGroup::advanced, "spectrum3d.zoom", "Camera Zoom", 0.6f, 2, 0.05f, 1),
    REAL(View::spectrum3d, ControlGroup::quick, "spectrum3d.depth_scale", "Depth Spacing", 0.5f, 2, 0.05f, 1),
    REAL(View::spectrum3d, ControlGroup::advanced, "spectrum3d.z_gain", "Amplitude Height", 0.5f, 4, 0.1f, 1),
    CH(View::spectrum3d, ControlGroup::quick, "spectrum3d.mesh_mode", "Render Mode", "Lines|Surface|Points", 0),
    CH(View::spectrum3d, ControlGroup::display, "spectrum3d.color_mode", "Color By", "Amplitude|Age|Hybrid", 0),
    CH(View::spectrum3d, ControlGroup::advanced, "spectrum3d.line_decimation", "Line Detail", "Full|1/2|1/4", 0),
    CH(View::spectrum3d, ControlGroup::advanced, "spectrum3d.auto_orbit", "Auto Orbit", "Off|Slow|Medium", 0),
    ACT(View::spectrum3d, "spectrum3d.reset_camera", "Reset Camera"),

    CH(View::constellation, ControlGroup::quick, "constellation.source", "Source", "Raw IQ|Filtered Channel IQ|Recovered Symbols", 1),
    CH(View::constellation, ControlGroup::quick, "constellation.profile", "Expected Mode", "Auto|Raw|BPSK|QPSK|8PSK|16QAM|64QAM|FM Phase", 0),
    REAL(View::constellation, ControlGroup::advanced, "constellation.channel_bw_hz", "Channel BW", 2400, 250000, 100, 12500),
    CH(View::constellation, ControlGroup::quick, "constellation.points", "Visible Points", "256|512|1024|2048|4096|8192", 3),
    REAL(View::constellation, ControlGroup::quick, "constellation.persistence_s", "Persistence", 0, 5, 0.1f, 1),
    INT(View::constellation, ControlGroup::advanced, "constellation.point_size", "Point Size", 1, 3, 1, 1),
    CH(View::constellation, ControlGroup::quick, "constellation.normalize", "Normalize", "Off|RMS|Peak", 1),
    CH(View::constellation, ControlGroup::advanced, "constellation.axis_scale", "Axis Scale", "Auto|0.5|1.0|1.5|2.0", 0),
    TOG(View::constellation, ControlGroup::advanced, "constellation.dc_remove", "Remove DC", 1),
    REAL(View::constellation, ControlGroup::advanced, "constellation.phase_deg", "Phase Rotate", -180, 180, 1, 0),
    CAP(View::constellation, ControlGroup::advanced, ControlKind::choice, "constellation.carrier_recovery", "Carrier Recovery", 0, 2, 1, 0, "Off|PLL|Costas", cap_filtered_iq, true, "Filtered channel IQ required"),
    CAP(View::constellation, ControlGroup::advanced, ControlKind::choice, "constellation.timing_recovery", "Timing Recovery", 0, 2, 1, 0, "Off|Gardner|Mueller & Muller", cap_filtered_iq, true, "Filtered channel IQ required"),
    REAL(View::constellation, ControlGroup::advanced, "constellation.symbol_rate", "Symbol Rate", 0, 250000, 100, 0),
    CH(View::constellation, ControlGroup::display, "constellation.show_ideal", "Ideal Overlay", "Off|Points|Decision Regions", 0),
    ACT(View::constellation, "constellation.clear", "Clear Persistence"),

    CH(View::iqscope, ControlGroup::quick, "iqscope.traces", "Traces", "I|Q|I + Q|Magnitude + Phase", 2),
    CH(View::iqscope, ControlGroup::quick, "iqscope.timebase", "Time / Division", "50us|100us|200us|500us|1ms|2ms|5ms|10ms|20ms|50ms|100ms", 3),
    REAL(View::iqscope, ControlGroup::quick, "iqscope.vertical_scale", "Units / Division", 0.1f, 2, 0.1f, 0.5f),
    REAL(View::iqscope, ControlGroup::advanced, "iqscope.vertical_position", "Position", -2, 2, 0.1f, 0),
    CH(View::iqscope, ControlGroup::advanced, "iqscope.coupling", "Coupling", "DC|AC", 0),
    CH(View::iqscope, ControlGroup::quick, "iqscope.trigger_mode", "Trigger", "Auto|Normal|Single", 0),
    CH(View::iqscope, ControlGroup::advanced, "iqscope.trigger_source", "Trigger Source", "I|Q|Magnitude", 0),
    REAL(View::iqscope, ControlGroup::quick, "iqscope.trigger_level", "Trigger Level", -1, 1, 0.05f, 0),
    CH(View::iqscope, ControlGroup::advanced, "iqscope.trigger_slope", "Trigger Edge", "Rising|Falling|Either", 0),
    INT(View::iqscope, ControlGroup::advanced, "iqscope.pretrigger_pct", "Pre-Trigger", 0, 80, 5, 20),
    CH(View::iqscope, ControlGroup::advanced, "iqscope.decimation", "Decimation", "Auto|1|2|4|8|16|32|64|128", 0),
    CH(View::iqscope, ControlGroup::display, "iqscope.interpolation", "Trace Interpolation", "None|Linear", 1),
    ACT(View::iqscope, "iqscope.arm_single", "Arm Single"),

    CH(View::polar, ControlGroup::quick, "polar.source", "Source", "Raw IQ|Filtered Channel IQ|Recovered Symbols", 1),
    CH(View::polar, ControlGroup::quick, "polar.mode", "View Mode", "Vectors|Trail|Density", 1),
    CH(View::polar, ControlGroup::advanced, "polar.phase_reference", "Phase Reference", "Fixed 0|Manual|Auto Lock", 0),
    REAL(View::polar, ControlGroup::quick, "polar.rotation_deg", "Rotate", -180, 180, 1, 0),
    CH(View::polar, ControlGroup::advanced, "polar.radial_scale", "Radial Scale", "Auto|0.25x|0.5x|1x|1.5x|2x", 0),
    CH(View::polar, ControlGroup::quick, "polar.normalize", "Normalize", "Off|RMS|Peak", 1),
    REAL(View::polar, ControlGroup::quick, "polar.persistence_s", "Persistence", 0, 10, 0.1f, 1),
    REAL(View::polar, ControlGroup::advanced, "polar.magnitude_gate_db", "Magnitude Gate", -60, 0, 1, -30),
    INT(View::polar, ControlGroup::display, "polar.trail_width", "Trail Width", 1, 3, 1, 1),
    CH(View::polar, ControlGroup::display, "polar.angle_labels", "Angle Labels", "Off|90 degrees|45 degrees", 2),
    CH(View::polar, ControlGroup::display, "polar.show_histogram", "Phase Histogram", "Off|Outer Ring", 0),
    ACT(View::polar, "polar.clear", "Clear Trail"),

    CH(View::occupancy, ControlGroup::advanced, "occupancy.band_plan", "Band Plan", "Automatic Equal Bands|Custom Edges|Saved Channel Plan", 0),
    CH(View::occupancy, ControlGroup::quick, "occupancy.band_count", "Bands", "4|8|16|32", 1),
    CH(View::occupancy, ControlGroup::advanced, "occupancy.threshold_mode", "Busy Threshold", "Absolute|Noise Relative|Adaptive", 1),
    REAL(View::occupancy, ControlGroup::quick, "occupancy.threshold_offset_db", "Above Noise", 3, 30, 1, 10),
    REAL(View::occupancy, ControlGroup::advanced, "occupancy.absolute_dbfs", "Absolute Level", -130, -20, 1, -80),
    INT(View::occupancy, ControlGroup::advanced, "occupancy.minimum_dwell_ms", "Minimum Dwell", 10, 10000, 10, 100),
    CH(View::occupancy, ControlGroup::quick, "occupancy.integration", "Statistics Window", "1 min|5 min|15 min|60 min|Session", 1),
    CH(View::occupancy, ControlGroup::quick, "occupancy.time_bin_ms", "Heatmap Time Bin", "100|250|500|1000|2000|5000|10000", 3),
    CH(View::occupancy, ControlGroup::advanced, "occupancy.basis", "Occupancy Basis", "Busy Time|Event Count", 0),
    TOG(View::occupancy, ControlGroup::advanced, "occupancy.alert_enabled", "Alert", 0),
    INT(View::occupancy, ControlGroup::advanced, "occupancy.alert_pct", "Alert Above", 1, 100, 1, 80),
    CH(View::occupancy, ControlGroup::advanced, "occupancy.sort_bars", "Bar Order", "Frequency|Highest Occupancy", 0),
    CAP(View::occupancy, ControlGroup::quick, ControlKind::action, "occupancy.reset_session", "Reset Statistics", 0, 0, 0, 0, "", cap_none, true, ""),
    CAP(View::occupancy, ControlGroup::action, ControlKind::action, "occupancy.save_csv", "Save CSV", 0, 0, 0, 0, "", cap_sd, true, "Writable SD card required"),

    TOG(View::peak_average, ControlGroup::quick, "peakavg.show_live", "Live Trace", 1),
    TOG(View::peak_average, ControlGroup::quick, "peakavg.show_average", "Average Trace", 1),
    TOG(View::peak_average, ControlGroup::quick, "peakavg.show_max", "Max Hold Trace", 1),
    CH(View::peak_average, ControlGroup::advanced, "peakavg.average_mode", "Average Type", "EMA Power|Linear Power", 0),
    REAL(View::peak_average, ControlGroup::quick, "peakavg.average_time_s", "Average Time", 0.1f, 10, 0.1f, 1),
    CH(View::peak_average, ControlGroup::advanced, "peakavg.live_smoothing", "Live Smoothing", "Off|Light|Medium|Heavy", 1),
    CH(View::peak_average, ControlGroup::advanced, "peakavg.max_mode", "Max Hold Mode", "Infinite|Timed|Decay", 2),
    REAL(View::peak_average, ControlGroup::advanced, "peakavg.hold_time_s", "Hold Time", 1, 60, 1, 10),
    REAL(View::peak_average, ControlGroup::advanced, "peakavg.decay_db_s", "Hold Decay", 0.5f, 30, 0.5f, 3),
    INT(View::peak_average, ControlGroup::advanced, "peakavg.marker_count", "Peak Markers", 0, 8, 1, 0),
    TOG(View::peak_average, ControlGroup::display, "peakavg.shared_scale", "Shared Scale", 1),
    INT(View::peak_average, ControlGroup::display, "peakavg.trace_width", "Trace Width", 1, 3, 1, 1),
    CAP(View::peak_average, ControlGroup::quick, ControlKind::action, "peakavg.clear_hold", "Clear Max Hold", 0, 0, 0, 0, "", cap_none, true, ""),
    ACT(View::peak_average, "peakavg.clear_average", "Restart Average"),

    CH(View::doppler, ControlGroup::quick, "doppler.span_hz", "Analysis Span", "250|500|1000|2000|5000", 3),
    CH(View::doppler, ControlGroup::advanced, "doppler.fft_size", "FFT Size", "1024|2048|4096|8192", 2),
    CH(View::doppler, ControlGroup::advanced, "doppler.window", "Window", "Hann|Blackman-Harris|Flat Top", 1),
    CH(View::doppler, ControlGroup::quick, "doppler.integration", "Integrate", "1|2|4|8|16|32|64", 3),
    CH(View::doppler, ControlGroup::advanced, "doppler.history_s", "History", "10|30|60|300|900", 2),
    CH(View::doppler, ControlGroup::quick, "doppler.tracking", "Tracker", "Off|Peak|Centroid|PLL", 2),
    REAL(View::doppler, ControlGroup::quick, "doppler.track_window_hz", "Track Window", 10, 500, 10, 100),
    REAL(View::doppler, ControlGroup::advanced, "doppler.threshold_db", "Track Threshold", 3, 30, 1, 8),
    CH(View::doppler, ControlGroup::advanced, "doppler.reference", "Zero Reference", "Tuned Center|Fixed Frequency|Zero at Lock", 2),
    REAL(View::doppler, ControlGroup::advanced, "doppler.fixed_reference_hz", "Fixed Reference", 100000, 1800000000.0f, 1, 100000000),
    CH(View::doppler, ControlGroup::advanced, "doppler.detrend", "Remove Trend", "Off|Linear|Quadratic", 0),
    CAP(View::doppler, ControlGroup::advanced, ControlKind::choice, "doppler.units", "Units", 0, 2, 1, 0, "Hz|kHz|m/s", cap_carrier_frequency, false, "Carrier frequency required for m/s"),
    CH(View::doppler, ControlGroup::display, "doppler.show_fit", "Fit Overlay", "Off|Linear|Quadratic", 0),
    CAP(View::doppler, ControlGroup::quick, ControlKind::action, "doppler.lock_track", "Lock Selected Track", 0, 0, 0, 0, "", cap_none, true, ""),
    ACT(View::doppler, "doppler.clear", "Clear History"),
    CAP(View::doppler, ControlGroup::action, ControlKind::action, "doppler.save_csv", "Save Track CSV", 0, 0, 0, 0, "", cap_sd, true, "Writable SD card required"),

    CH(View::channelizer, ControlGroup::quick, "channelizer.count", "Channels", "2|4|8|16", 2),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.layout", "Layout", "Auto|1xN|2xN|4xN", 0),
    CH(View::channelizer, ControlGroup::quick, "channelizer.plan", "Channel Plan", "Contiguous|Custom Frequencies|Saved Bookmarks", 0),
    CH(View::channelizer, ControlGroup::quick, "channelizer.bandwidth", "Channel BW", "Auto|6.25|12.5|25|50|100|125|250 kHz", 0),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.demod", "Selected Demod", "NFM|AM|USB|LSB|WFM", 0),
    REAL(View::channelizer, ControlGroup::advanced, "channelizer.squelch_dbfs", "Selected Squelch", -120, -20, 1, -90),
    REAL(View::channelizer, ControlGroup::advanced, "channelizer.selected_offset_hz", "Selected Offset", -1200000, 1200000, 100, 0),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.guard_pct", "Guard", "0%|5%|10%|20%", 2),
    CH(View::channelizer, ControlGroup::quick, "channelizer.scale_mode", "Vertical Scale", "Shared|Per Tile", 0),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.metric", "Meter", "Peak|RMS|Average Power", 1),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.threshold_mode", "Activity Threshold", "Absolute|Noise Relative", 1),
    REAL(View::channelizer, ControlGroup::advanced, "channelizer.threshold_db", "Above Noise", 3, 30, 1, 10),
    CH(View::channelizer, ControlGroup::quick, "channelizer.refresh_hz", "Tile Refresh", "5|10|15|20|30", 2),
    CH(View::channelizer, ControlGroup::advanced, "channelizer.selected_action", "Tap Action", "Select|Tune|Solo Audio", 0),
    CH(View::channelizer, ControlGroup::display, "channelizer.tile_labels", "Labels", "Channel #|Frequency|Custom Name|Channel # + Frequency", 3),
    ACT(View::channelizer, "channelizer.edit_channel", "Edit Selected"),
    CAP(View::channelizer, ControlGroup::advanced, ControlKind::toggle, "channelizer.solo", "Solo Selected", 0, 1, 1, 0, "Off|On", cap_channel_audio, false, "Audio limit reached for this quality profile"),
    CAP(View::channelizer, ControlGroup::advanced, ControlKind::toggle, "channelizer.mute", "Mute Selected", 0, 1, 1, 0, "Off|On", cap_channel_audio, false, "Audio limit reached for this quality profile"),
    ACT(View::channelizer, "channelizer.reset_plan", "Reset Plan"),

    CH(View::audio_spectrogram, ControlGroup::quick, "audiospec.source", "Audio Source", "Current Demod|Left|Right|L+R Mix|Microphone|Line Input", 0),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.sample_rate", "Analysis Rate", "Auto|8|12|16|24|48 kHz", 0),
    CH(View::audio_spectrogram, ControlGroup::quick, "audiospec.fft_size", "FFT Size", "256|512|1024|2048", 2),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.window", "Window", "Rectangular|Hann|Hamming|Blackman-Harris", 1),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.overlap", "Overlap", "0%|25%|50%|75%", 2),
    CH(View::audio_spectrogram, ControlGroup::quick, "audiospec.max_frequency", "Maximum Frequency", "Auto Nyquist|4|8|12|24 kHz", 0),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.frequency_scale", "Frequency Scale", "Linear|Log", 0),
    REAL(View::audio_spectrogram, ControlGroup::display, "audiospec.floor_dbfs", "Audio Floor", -140, -20, 1, -100),
    REAL(View::audio_spectrogram, ControlGroup::display, "audiospec.ceiling_dbfs", "Audio Ceiling", -100, 0, 1, -20),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.normalization", "Level Tracking", "Off|Slow|Per Frame", 1),
    CH(View::audio_spectrogram, ControlGroup::quick, "audiospec.palette", "Palette", "Classic|Turbo|Viridis|Inferno|Grayscale", 0),
    CH(View::audio_spectrogram, ControlGroup::quick, "audiospec.time_span_s", "Visible Time", "5|10|20|30|60", 1),
    CH(View::audio_spectrogram, ControlGroup::advanced, "audiospec.preemphasis", "Pre-Emphasis", "Off|50 us|75 us", 0),
    CH(View::audio_spectrogram, ControlGroup::display, "audiospec.filter_overlay", "Audio Filter", "Off|Edges|Shaded Passband", 1),
    CH(View::audio_spectrogram, ControlGroup::display, "audiospec.cursor", "Cursor Readout", "Off|Tap|Locked", 1),
    ACT(View::audio_spectrogram, "audiospec.clear", "Clear History"),
};

#undef TOG
#undef INT
#undef REAL
#undef CH
#undef ACT
#undef CAP

constexpr const char* kNames[] = {
    "SPECTRUM / FFT", "CLASSIC WATERFALL", "PHOSPHOR PERSISTENCE",
    "3D SPECTRUM HISTORY", "I/Q CONSTELLATION", "I/Q OSCILLOSCOPE",
    "POLAR / PHASE", "CHANNEL OCCUPANCY", "PEAK HOLD / AVERAGE",
    "DOPPLER / DRIFT", "CHANNELIZED TILES", "AUDIO SPECTROGRAM"};
constexpr const char* kSlugs[] = {
    "spectrum", "waterfall", "phosphor", "spectrum3d", "constellation", "iqscope",
    "polar", "occupancy", "peakavg", "doppler", "channelizer", "audiospec"};

}  // namespace

const ControlDescriptor* controls(size_t* count) {
  if (count) *count = sizeof(kControls) / sizeof(kControls[0]);
  return kControls;
}

const ControlDescriptor* find_control(const char* id, size_t* index) {
  if (!id) return nullptr;
  for (size_t i = 0; i < sizeof(kControls) / sizeof(kControls[0]); ++i) {
    if (strcmp(kControls[i].id, id) == 0) {
      if (index) *index = i;
      return &kControls[i];
    }
  }
  return nullptr;
}

const char* view_name(View view) {
  const auto i = static_cast<size_t>(view);
  return i < static_cast<size_t>(View::count) ? kNames[i] : "VISUALIZER";
}

const char* view_slug(View view) {
  const auto i = static_cast<size_t>(view);
  return i < static_cast<size_t>(View::count) ? kSlugs[i] : "unknown";
}

bool parse_view(const char* text, View* view) {
  if (!text || !view) return false;
  for (size_t i = 0; i < static_cast<size_t>(View::count); ++i) {
    if (strcmp(text, kSlugs[i]) == 0) {
      *view = static_cast<View>(i);
      return true;
    }
  }
  return false;
}

bool controls_self_check() {
  if (sizeof(kNames) / sizeof(kNames[0]) != static_cast<size_t>(View::count)) return false;
  for (size_t i = 0; i < sizeof(kControls) / sizeof(kControls[0]); ++i) {
    const auto& c = kControls[i];
    if (!c.id || !c.id[0] || !c.label || !c.label[0]) return false;
    if (c.kind != ControlKind::action && (c.maximum < c.minimum || c.step <= 0)) return false;
    if (c.kind != ControlKind::action &&
        (c.default_value < c.minimum || c.default_value > c.maximum)) return false;
    for (size_t j = i + 1; j < sizeof(kControls) / sizeof(kControls[0]); ++j)
      if (strcmp(c.id, kControls[j].id) == 0) return false;
  }
  return find_control("visual.freeze") && find_control("fft.size") &&
         find_control("channelizer.solo") && find_control("audiospec.clear");
}

}  // namespace orcsdr::visualizer
