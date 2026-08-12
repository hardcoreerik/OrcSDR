#pragma once

#include <cstdint>
#include <cstddef>

/** Live snapshot shared between RF core and HTTP handlers. */
namespace orcsdr::web {

constexpr size_t kMaxAircraft = 16;

struct Aircraft {
  uint32_t icao = 0;
  char callsign[9]{};
  char registration[9]{};
  char type[49]{};
  char owner[51]{};
  int altitude_ft = 0;
  int speed_kts = 0;
  int heading_deg = 0;
  int vertical_rate_fpm = 0;
  float latitude = 0;
  float longitude = 0;
  float signal_dbfs = 0;
  bool has_callsign = false;
  bool has_altitude = false;
  bool has_speed = false;
  bool has_heading = false;
  bool has_vertical_rate = false;
  bool has_position = false;
  uint32_t messages = 0;
  uint32_t age_ms = 0;
};

struct Snapshot {
  Aircraft aircraft[kMaxAircraft]{};
  uint8_t aircraft_count = 0;
  uint32_t revision = 0;
  uint32_t total_messages = 0;
  uint32_t total_crc_ok = 0;
  uint32_t adsb_preambles = 0;
  uint32_t adsb_frames = 0;
  uint32_t adsb_df17 = 0;
  uint16_t adsb_mag_min = 0xffff;
  uint16_t adsb_mag_max = 0;
  float message_rate = 0;
  float strongest_signal_dbfs = -90.0f;
  bool live = false;
  bool rtl_ready = false;
  bool streaming = false;
  char mode[12] = "ADSB";
  char product[48] = "-";
  char status[48] = "-";
  uint32_t frequency_hz = 0;
  uint32_t sample_rate_sps = 0;
  uint32_t effective_sps = 0;
  uint32_t iq_drops = 0;
  uint32_t free_heap = 0;
  char ip[16] = "0.0.0.0";
  bool eth_link = false;
  uint16_t radar_range_nm = 25;
  bool location_configured = false;
  double latitude = 0;
  double longitude = 0;
  /* FM / WX browser radio path */
  float fm_signal_dbfs = -90.0f;
  float fm_spectrum[64]{};
  uint8_t fm_spectrum_bins = 64;
  uint32_t pcm_underruns = 0;
  uint32_t pcm_overruns = 0;
  uint32_t pcm_available = 0;
  uint32_t pcm_sequence = 0;
};

/** Thread-safe publish from RF loop; HTTP handlers take a copy. */
void publish_snapshot(const Snapshot& snapshot);
void copy_snapshot(Snapshot* out);

/** Optional receiver location for radar geometry (e7 degrees). */
void set_receiver_location(bool configured, double latitude, double longitude,
                           uint16_t radar_range_nm);
void get_receiver_location(bool* configured, double* latitude, double* longitude,
                           uint16_t* radar_range_nm);

}  // namespace orcsdr::web
