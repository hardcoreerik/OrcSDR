#include "session_identity.hpp"

#include <cstdio>

namespace orcsdr::session_identity {

const char kHello[] =
    "{\"type\":\"hello\",\"message_id\":\"m5tab5_hello_01\","
    "\"protocol_version\":{\"major\":1,\"minor\":0},"
    "\"payload\":{\"adapter\":{\"id\":\"m5tab5.serial\",\"version\":\"0.8.31\"},"
    "\"features\":[\"telemetry\",\"display\",\"touch\",\"authenticated_session\",\"journal\",\"network_inventory\",\"wifi_provisioning\",\"power_telemetry\",\"usb_host\",\"rtl_sdr_probe\",\"rtl_sdr_radio_ui\",\"rtl_sdr_scope_scroll\"],"
    "\"capability_namespaces\":[\"m5tab5\"],\"config_schema_version\":1}}\n";

int format_snapshot(const Snapshot& s, char* out, size_t size) {
  return snprintf(out, size,
      "{\"type\":\"node_snapshot\",\"message_id\":\"m5tab5_snapshot_%lu\","
      "\"protocol_version\":{\"major\":1,\"minor\":0},"
      "\"payload\":{\"node_id\":\"%s\",\"connection_state\":\"online\","
      "\"hardware\":{\"chip\":\"%s\",\"revision\":%u,\"cores\":%u,"
      "\"flash_bytes\":%u,\"psram_bytes\":%u,\"free_heap_bytes\":%u,"
      "\"display\":{\"width\":%d,\"height\":%d,\"touch_ready\":%s}},"
      "\"network\":{\"transport\":\"esp_hosted_sdio\",\"station_ready\":%s,"
      "\"configured\":%s,\"scan_result\":%d,\"connected\":%s,\"ip\":\"%s\"},"
      "\"power\":{\"source\":\"m5unified\",\"pmic_detected\":%s,\"pmic_type\":%d,"
      "\"battery_level\":%ld,\"battery_mv\":%d,"
      "\"battery_ma\":%ld,\"vbus_mv\":%d,\"charging\":\"%s\"},"
      "\"journal\":{\"latest_sequence\":%lu,\"acknowledged_sequence\":%lu,\"pending\":%u,\"dropped\":%lu},"
      "\"offline_workflow\":{\"id\":\"status_on_disconnect\",\"config_revision\":%lu,"
      "\"runs\":%u,\"max_runs\":%u},"
      "\"capabilities\":[\"m5tab5.device.info\",\"m5tab5.health.read\","
      "\"m5tab5.display.status\",\"m5tab5.input.touch\",\"m5tab5.event.journal.sync\","
      "\"m5tab5.network.inventory\",\"m5tab5.network.configure\","
      "\"m5tab5.power.read\"]}}\n",
      s.uptime_ms, s.node_id, s.chip,
      s.revision, s.cores, s.flash_bytes,
      s.psram_bytes, s.free_heap_bytes, s.display_width, s.display_height,
      s.touch_ready ? "true" : "false",
      s.station_ready ? "true" : "false", s.wifi_configured ? "true" : "false",
      s.scan_result, s.wifi_connected ? "true" : "false", s.ip,
      s.pmic_detected ? "true" : "false",
      s.pmic_type, s.battery_level, s.battery_mv,
      s.battery_ma, s.vbus_mv, s.charging,
      s.journal_latest_sequence,
      s.journal_acknowledged_sequence, s.journal_pending,
      s.journal_dropped,
      s.workflow_revision, s.workflow_runs, s.workflow_max_runs);
}

void emit(const Snapshot& snapshot, void (*write)(const char* text)) {
  write(kHello);
  char line[kSnapshotLineMax];
  if (format_snapshot(snapshot, line, sizeof(line)) > 0) write(line);
}

}  // namespace orcsdr::session_identity
