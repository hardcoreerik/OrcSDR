#pragma once
// Pure C++17, fixed-memory DMR Tier III / channel-role parser for OrcSDR.
// Classifies validated receive events (VOICE, CTRL, DATA, MIXED), extracts Tier III
// SYScode (Model, Net ID, Site ID) from CRC-validated C_ALOHA / C_BCAST CSBKs,
// and harvests verified neighbor / granted frequencies from MBC (dt=4 + dt=5) pairs.
#include "dv_radio.hpp"
#include "dv_scanner.hpp"
#include <cstdio>
#include <cstdint>

namespace orcsdr::dv {

constexpr uint8_t kRoleControl = 1u << 0;
constexpr uint8_t kRoleVoice   = 1u << 1;
constexpr uint8_t kRoleData    = 1u << 2;

inline const char* role_label(uint8_t role_flags, bool suspected_ghost = false) noexcept {
  if (suspected_ghost) return "GHOST?";
  const bool v = (role_flags & kRoleVoice) != 0;
  const bool c = (role_flags & kRoleControl) != 0;
  const bool d = (role_flags & kRoleData) != 0;
  if (v && c) return "MIXED";
  if (v) return "VOICE";
  if (c) return "CTRL";
  if (d) return "DATA";
  return "";
}

struct DmrControlInfo {
  uint8_t role_flags = 0;
  bool has_sys = false;
  uint8_t model = 0;       // ETSI TS 102 361-4 §B.1.1: 0 Tiny, 1 Small, 2 Large, 3 Huge
  uint16_t sys_code = 0;   // 14-bit SYScode
  uint16_t net_id = 0;     // 1-based Net ID
  uint16_t site_id = 0;    // 1-based Site ID
  bool has_harvest_hz = false;
  uint32_t harvest_hz = 0;
  bool is_grant = false;
  bool is_adj_site = false;
};

inline void decode_t3_syscode(uint16_t syscode16, DmrControlInfo& out) noexcept {
  out.has_sys = true;
  out.model = static_cast<uint8_t>((syscode16 >> 14) & 0x3u);
  const uint16_t sys14 = static_cast<uint16_t>(syscode16 & 0x3FFFu);
  out.sys_code = sys14;
  switch (out.model) {
    case 0: // Tiny: 9-bit Net, 5-bit Site
      out.net_id = static_cast<uint16_t>(((sys14 >> 5) & 0x1FFu) + 1u);
      out.site_id = static_cast<uint16_t>((sys14 & 0x1Fu) + 1u);
      break;
    case 1: // Small: 7-bit Net, 7-bit Site
      out.net_id = static_cast<uint16_t>(((sys14 >> 7) & 0x7Fu) + 1u);
      out.site_id = static_cast<uint16_t>((sys14 & 0x7Fu) + 1u);
      break;
    case 2: // Large: 4-bit Net, 10-bit Site
      out.net_id = static_cast<uint16_t>(((sys14 >> 10) & 0xFu) + 1u);
      out.site_id = static_cast<uint16_t>((sys14 & 0x3FFu) + 1u);
      break;
    case 3: // Huge: 2-bit Net, 12-bit Site
    default:
      out.net_id = static_cast<uint16_t>(((sys14 >> 12) & 0x3u) + 1u);
      out.site_id = static_cast<uint16_t>((sys14 & 0xFFFu) + 1u);
      break;
  }
}

inline void format_net_site(uint16_t net_id, uint16_t site_id, char* out, size_t cap) noexcept {
  if (!out || cap == 0) return;
  if (net_id == 0) { out[0] = 0; return; }
  if (site_id > 0) std::snprintf(out, cap, "NET %u S%u", unsigned(net_id), unsigned(site_id));
  else std::snprintf(out, cap, "NET %u", unsigned(net_id));
}

inline bool is_t3_control_csbko(uint8_t csbko) noexcept {
  switch (csbko) {
    case 0x19: // C_ALOHA
    case 0x1C: // C_AHOY
    case 0x20: // C_ACKD
    case 0x28: // C_BCAST
    case 0x2F: // P_PROTECT
    case 0x30: // TV_GRANT
    case 0x31: // BTV_GRANT
    case 0x33: // PD_GRANT
    case 0x34: // PV_GRANT
      return true;
    default:
      return false;
  }
}

inline uint32_t decode_abs_freq_23(uint32_t raw23) noexcept {
  const uint32_t mhz = (raw23 >> 13) & 0x3FFu;
  const uint32_t khz125 = raw23 & 0x1FFFu;
  const uint32_t hz = mhz * 1000000u + khz125 * 125u;
  if (hz < kMinChannelHz || hz > kMaxChannelHz) return 0;
  return hz;
}

inline DmrControlInfo parse_dmr_event(const dv_radio::Event& e) noexcept {
  using digital_voice_ui::Evidence;
  using digital_voice_ui::NxdnChannel;
  using digital_voice_ui::Protocol;
  DmrControlInfo info;
  if (e.evidence < Evidence::integrity_valid) return info;

  if (e.protocol == Protocol::nxdn48 || e.protocol == Protocol::nxdn96) {
    if (e.nxdn_rf_valid) {
      if (e.nxdn_rf_channel == 0u) info.role_flags |= kRoleControl;       // RCCH
      else if (e.nxdn_rf_channel == 1u || e.nxdn_rf_channel == 2u) info.role_flags |= kRoleVoice; // RTCH / RDCH
    }
    return info;
  }

  if (e.protocol != Protocol::dmr) return info;

  if (e.evidence >= Evidence::identity_valid || e.late_entry ||
      (e.has_data_type && (e.data_type == 1u || e.data_type == 2u))) {
    info.role_flags |= kRoleVoice;
  }

  if (e.has_data_type) {
    if (e.data_type == 6u || e.data_type == 11u) {
      info.role_flags |= kRoleData;
    } else if (e.data_type == 4u || e.data_type == 5u) {
      info.role_flags |= kRoleControl;
    } else if (e.data_type == 3u && e.info_valid) {
      const uint8_t csbko = static_cast<uint8_t>(e.info[0] & 0x3Fu);
      if (is_t3_control_csbko(csbko)) {
        info.role_flags |= kRoleControl;
        // Both C_ALOHA (0x19) and C_BCAST (0x28) carry 16-bit SYScode in bytes 4..6
        // (note: C_BCAST byte 1 may be non-zero e.g. 0x08/0x68 for standard Tier III).
        if (csbko == 0x19u || csbko == 0x28u) {
          const uint16_t sys16 = static_cast<uint16_t>(
              ((static_cast<uint16_t>(e.info[4] & 0x03u)) << 14) |
              (static_cast<uint16_t>(e.info[5]) << 6) |
              (static_cast<uint16_t>(e.info[6]) >> 2));
          decode_t3_syscode(sys16, info);
          if (csbko == 0x28u && ((e.info[2] >> 3) & 0x1Fu) == 6u) info.is_adj_site = true;
        } else if (csbko >= 0x30u && csbko <= 0x34u) {
          info.is_grant = true;
        }
      }
    }
  }
  return info;
}

class MbcTracker {
 public:
  void reset() noexcept {
    for (size_t i = 0; i < 3; ++i) hdr_valid_[i] = false;
  }

  DmrControlInfo on_event(const dv_radio::Event& e) noexcept {
    DmrControlInfo info = parse_dmr_event(e);
    if (e.protocol != digital_voice_ui::Protocol::dmr || !e.has_data_type || !e.info_valid ||
        e.evidence < digital_voice_ui::Evidence::integrity_valid) {
      return info;
    }
    const size_t slot_idx = (e.slot_valid && (e.slot == 1u || e.slot == 2u)) ? static_cast<size_t>(e.slot) : 0u;
    if (e.data_type == 4u) {
      const uint8_t csbko = static_cast<uint8_t>(e.info[0] & 0x3Fu);
      if (is_t3_control_csbko(csbko)) {
        hdr_valid_[slot_idx] = true;
        hdr_cc_[slot_idx] = e.colour_code_valid ? e.colour_code : 0xFFu;
        for (size_t i = 0; i < 10; ++i) hdr_[slot_idx][i] = e.info[i];
        if (csbko == 0x28u) {
          const uint16_t sys16 = static_cast<uint16_t>(
              ((static_cast<uint16_t>(e.info[4] & 0x03u)) << 14) |
              (static_cast<uint16_t>(e.info[5]) << 6) |
              (static_cast<uint16_t>(e.info[6]) >> 2));
          decode_t3_syscode(sys16, info);
          if (((e.info[2] >> 3) & 0x1Fu) == 6u) info.is_adj_site = true;
        }
      }
    } else if (e.data_type == 5u) {
      size_t idx = slot_idx;
      if (!hdr_valid_[idx] && slot_idx == 0u) {
        for (size_t i = 0; i < 3; ++i) {
          if (hdr_valid_[i] && (!e.colour_code_valid || hdr_cc_[i] == e.colour_code)) { idx = i; break; }
        }
      }
      if (hdr_valid_[idx] && (!e.colour_code_valid || hdr_cc_[idx] == e.colour_code)) {
        const uint8_t hdr_csbko = static_cast<uint8_t>(hdr_[idx][0] & 0x3Fu);
        const uint8_t cont_csbko = static_cast<uint8_t>(e.info[0] & 0x3Fu);
        if (hdr_csbko == cont_csbko) {
          // Bottom 23 bits of MBCC CDEFParms carry BS TX / MS RX absolute frequency (10-bit MHz + 13-bit 125 Hz units).
          const uint32_t raw23 =
              ((static_cast<uint32_t>(e.info[7] & 0x7Fu)) << 16) |
              (static_cast<uint32_t>(e.info[8]) << 8) |
              static_cast<uint32_t>(e.info[9]);
          const uint32_t hz = decode_abs_freq_23(raw23);
          if (hdr_csbko == 0x28u && ((hdr_[idx][2] >> 3) & 0x1Fu) == 6u && (e.info[2] & 0x3Cu) == 0u) {
            info.is_adj_site = true;
            if (hz != 0u) { info.has_harvest_hz = true; info.harvest_hz = hz; }
          } else if (hdr_csbko >= 0x30u && hdr_csbko <= 0x34u) {
            info.is_grant = true;
            if (hz != 0u) { info.has_harvest_hz = true; info.harvest_hz = hz; }
          }
        }
        hdr_valid_[idx] = false;
      }
    }
    return info;
  }

 private:
  bool hdr_valid_[3]{};
  uint8_t hdr_cc_[3]{};
  uint8_t hdr_[3][10]{};
};

}  // namespace orcsdr::dv
