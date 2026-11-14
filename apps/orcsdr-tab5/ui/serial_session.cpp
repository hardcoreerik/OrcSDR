#include "serial_session.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace orcsdr::serial_session {
namespace {

// Constant time: the loop always runs over every byte.
bool same_bytes(const uint8_t* a, const uint8_t* b, size_t size) {
  uint8_t difference = 0;
  for (size_t index = 0; index < size; ++index) difference |= a[index] ^ b[index];
  return difference == 0;
}

// main.cpp's hmac_matches(): HMAC of the text with the pairing key, compared in constant time.
bool hmac_matches(const Hooks& hooks, const char* value, const uint8_t* candidate) {
  uint8_t expected[32];
  if (!hooks.hmac(reinterpret_cast<const uint8_t*>(value), strlen(value), expected)) return false;
  return same_bytes(expected, candidate, sizeof(expected));
}

}  // namespace

bool handle_command(char* command, bool authenticated, const Hooks& hooks) {
  if (strncmp(command, "PAIR ", 5) == 0) {
    uint8_t candidate[kPairingKeySize];
    if (!hooks.decode_hex(command + 5, candidate, sizeof(candidate))) {
      hooks.write_line("PAIR_INVALID");
      return true;
    }
    if (!hooks.paired()) {
      hooks.store_pairing_key(candidate);
      hooks.journal("paired");
    }
    hooks.write_line(same_bytes(hooks.pairing_key(), candidate, kPairingKeySize) ? "PAIR_OK" : "PAIR_LOCKED");
    return true;
  }
  if (strncmp(command, "AUTH ", 5) == 0 && hooks.paired()) {
    uint8_t nonce[16];
    uint8_t host_proof[32];
    uint8_t expected_host_proof[32];
    uint8_t signature[32];
    char* proof_hex = strchr(command + 5, ' ');
    if (proof_hex == nullptr) {
      hooks.write_line("AUTH_INVALID");
      return true;
    }
    *proof_hex++ = '\0';
    if (!hooks.decode_hex(command + 5, nonce, sizeof(nonce)) ||
        !hooks.decode_hex(proof_hex, host_proof, sizeof(host_proof))) {
      hooks.write_line("AUTH_INVALID");
      return true;
    }
    uint8_t host_input[sizeof("host") - 1 + sizeof(nonce)];
    memcpy(host_input, "host", sizeof("host") - 1);
    memcpy(host_input + sizeof("host") - 1, nonce, sizeof(nonce));
    if (!hooks.hmac(host_input, sizeof(host_input), expected_host_proof)) {
      hooks.write_line("AUTH_ERROR");
      return true;
    }
    if (!same_bytes(host_proof, expected_host_proof, sizeof(host_proof))) {
      hooks.write_line("AUTH_DENIED");
      return true;
    }
    uint8_t device_input[sizeof("device") - 1 + sizeof(nonce)];
    memcpy(device_input, "device", sizeof("device") - 1);
    memcpy(device_input + sizeof("device") - 1, nonce, sizeof(nonce));
    if (!hooks.hmac(device_input, sizeof(device_input), signature)) {
      hooks.write_line("AUTH_ERROR");
      return true;
    }
    hooks.write("AUTH_OK ");
    hooks.write_hex(signature, sizeof(signature));
    hooks.write_line("");
    hooks.set_authenticated(true);
    // An authenticated host (e.g. the UI regression scripts) takes over from the boot splash.
    hooks.session_started();
    return true;
  }
  if (strcmp(command, "PING") == 0 && authenticated) {
    hooks.ping();
    return true;
  }
  if (strncmp(command, "PREPARE_FLASH ", 14) == 0 && authenticated) {
    uint8_t digest[32];
    if (!hooks.decode_hex(command + 14, digest, sizeof(digest))) {
      hooks.write_line("FLASH_INVALID");
      return true;
    }
    hooks.show_status("Approved firmware update starting", Tone::approved);
    hooks.journal("firmware_approved");
    hooks.write("FLASH_READY ");
    hooks.write_hex(digest, sizeof(digest));
    hooks.write_line("");
    hooks.emit_identity();
    hooks.emit_pending_journal();
    return true;
  }
  if (strncmp(command, "SET_WIFI ", 9) == 0 && authenticated) {
    char* ssid_hex = command + 9;
    char* password_hex = strchr(ssid_hex, ' ');
    if (password_hex == nullptr) {
      hooks.write_line("WIFI_INVALID");
      return true;
    }
    *password_hex++ = '\0';
    char* signature_text = strchr(password_hex, ' ');
    if (signature_text == nullptr) {
      hooks.write_line("WIFI_INVALID");
      return true;
    }
    *signature_text++ = '\0';
    char candidate_ssid[kWifiSsidSize];
    char candidate_password[kWifiPasswordSize];
    uint8_t signature[32];
    char signed_value[208];
    if (strlen(ssid_hex) > 2 * (sizeof(candidate_ssid) - 1) ||
        strlen(password_hex) > 2 * (sizeof(candidate_password) - 1) ||
        strlen(signature_text) != 2 * sizeof(signature)) {
      hooks.write_line("WIFI_INVALID");
      return true;
    }
    snprintf(signed_value, sizeof(signed_value), "wifi|%s|%s", ssid_hex, password_hex);
    if (!hooks.decode_hex_text(ssid_hex, candidate_ssid, sizeof(candidate_ssid)) ||
        candidate_ssid[0] == '\0' ||
        !hooks.decode_hex_text(password_hex, candidate_password, sizeof(candidate_password)) ||
        !hooks.decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(hooks, signed_value, signature)) {
      hooks.write_line("WIFI_INVALID");
      return true;
    }
    hooks.store_wifi(candidate_ssid, candidate_password);
    hooks.journal("wifi_configured");
    hooks.write_line("WIFI_CONFIGURED");
    hooks.start_wifi();
    hooks.emit_identity();
    hooks.emit_pending_journal();
    return true;
  }
  if (strncmp(command, "INSTALL_STATUS ", 15) == 0 && authenticated) {
    char* revision_text = command + 15;
    char* max_runs_text = strchr(revision_text, ' ');
    if (max_runs_text == nullptr) {
      hooks.write_line("WORKFLOW_INVALID");
      return true;
    }
    *max_runs_text++ = '\0';
    char* signature_text = strchr(max_runs_text, ' ');
    if (signature_text == nullptr) {
      hooks.write_line("WORKFLOW_INVALID");
      return true;
    }
    *signature_text++ = '\0';
    const uint32_t revision = strtoul(revision_text, nullptr, 10);
    const uint32_t max_runs = strtoul(max_runs_text, nullptr, 10);
    uint8_t signature[32];
    char signed_value[48];
    snprintf(signed_value, sizeof(signed_value), "workflow|%lu|%lu",
             static_cast<unsigned long>(revision), static_cast<unsigned long>(max_runs));
    if (revision == 0 || max_runs == 0 || max_runs > UINT16_MAX ||
        !hooks.decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(hooks, signed_value, signature)) {
      hooks.write_line("WORKFLOW_INVALID");
      return true;
    }
    if (revision <= hooks.workflow_revision()) {
      hooks.write_line("WORKFLOW_STALE");
      return true;
    }
    hooks.install_workflow(revision, static_cast<uint16_t>(max_runs));
    hooks.journal("workflow_installed");
    char line[32];
    snprintf(line, sizeof(line), "WORKFLOW_OK %lu\n", static_cast<unsigned long>(revision));
    hooks.write(line);
    hooks.emit_identity();
    hooks.emit_pending_journal();
    return true;
  }
  if (strncmp(command, "ROTATE_KEY ", 11) == 0 && authenticated) {
    char* key_text = command + 11;
    char* signature_text = strchr(key_text, ' ');
    if (signature_text == nullptr) {
      hooks.write_line("ROTATE_INVALID");
      return true;
    }
    *signature_text++ = '\0';
    uint8_t replacement[kPairingKeySize];
    uint8_t signature[32];
    char signed_value[80];
    if (strlen(key_text) != 2 * sizeof(replacement) ||
        strlen(signature_text) != 2 * sizeof(signature)) {
      hooks.write_line("ROTATE_INVALID");
      return true;
    }
    snprintf(signed_value, sizeof(signed_value), "rotate|%s", key_text);
    if (!hooks.decode_hex(key_text, replacement, sizeof(replacement)) ||
        !hooks.decode_hex(signature_text, signature, sizeof(signature)) ||
        !hmac_matches(hooks, signed_value, signature)) {
      hooks.write_line("ROTATE_INVALID");
      return true;
    }
    hooks.store_pairing_key(replacement);
    hooks.journal("credential_rotated");
    hooks.write_line("KEY_ROTATED");
    hooks.set_authenticated(false);
    hooks.session_ended();
    hooks.show_status("Credential rotated - host reconnect required", Tone::warning);
    return true;
  }
  if (strcmp(command, "TEST_PRESSURE") == 0 && authenticated) {
    for (int index = 0; index < 10; ++index) hooks.journal("pressure_test");
    char line[32];
    snprintf(line, sizeof(line), "PRESSURE_OK %lu\n", static_cast<unsigned long>(hooks.dropped_events()));
    hooks.write(line);
    hooks.emit_identity();
    hooks.emit_pending_journal();
    return true;
  }
  if (strcmp(command, "PRESSURE_ACK") == 0 && authenticated) {
    hooks.clear_dropped_events();
    hooks.write_line("PRESSURE_CLEARED");
    hooks.emit_identity();
    return true;
  }
  if (strncmp(command, "ACK ", 4) == 0 && authenticated) {
    const uint32_t sequence = strtoul(command + 4, nullptr, 10);
    if (sequence <= hooks.next_sequence()) hooks.acknowledge(sequence);
    return true;
  }
  return false;
}

}  // namespace orcsdr::serial_session
