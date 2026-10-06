"""Compile the installed SDIO credit loop with a deterministic clock/probe.

Run after apply-esp-hosted-trampoline-fix.ps1. This tests the actual loop text,
not a second implementation of its timing policy. No device access required.
"""
import argparse
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'apps/orcsdr-tab5/managed_components/espressif__esp_hosted/host/mcu/eh_host_mcu_transport/src/eh_host_bus_sdio.c'

HARNESS = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#define BUFFER_UNAVAILABLE 0
#define BUFFER_AVAILABLE 1
#define SDIO_DRV_LOCK() ((void)0)
#define SDIO_DRV_UNLOCK() ((void)0)
#define ESP_LOGE(...) ((void)0)
#define EH_HOST_EVENT 0
#define EH_HOST_EVENT_TRANSPORT_FAILURE 1
#define ESP_OK 0
static int64_t now_us;
static int probe_us, ready_at, probes;
static uint32_t sdio_tx_unresponsive_streak;
static bool sdio_tx_unresponsive_reported;
static int bus_error, post_fails, post_attempts, notifications;
static int esp_event_post(int base, int id, void* data, int size, int wait) {
  (void)base; (void)id; (void)data; (void)size; (void)wait;
  ++post_attempts;
  if (post_fails) return -1;
  ++notifications; return ESP_OK;
}
static int64_t esp_timer_get_time(void) { return now_us; }
static int sdio_tx_credit_ready(uint32_t unused) {
  (void)unused;
  now_us += probe_us;
  ++probes;
  if (bus_error) ++sdio_tx_unresponsive_streak;
  return ready_at && probes >= ready_at ? BUFFER_AVAILABLE : BUFFER_UNAVAILABLE;
}
static void eh_host_port_task_delay_us(uint32_t us) { now_us += us; }
static int wait_for_credits(int wait_ms) {
  uint32_t buf_needed = 1;
  int got = BUFFER_UNAVAILABLE;
  /* LOOP */
  if (got != BUFFER_AVAILABLE) {
    /* REPORT */
  }
  return got;
}
static void check(const char* name, int ms, int cost, int ready, int expected) {
  now_us = 0; probes = 0; probe_us = cost; ready_at = ready;
  sdio_tx_unresponsive_streak = 0;
  int result = wait_for_credits(ms);
  int64_t bound = (int64_t)ms * 1000 + cost + 20;
  if (result != expected || now_us > bound || probes == 0) {
    fprintf(stderr, "%s FAIL: result=%d elapsed=%lld bound=%lld probes=%d\n",
            name, result, (long long)now_us, (long long)bound, probes);
    exit(1);
  }
  printf("%s PASS: elapsed=%lld probes=%d\n", name, (long long)now_us, probes);
}
int main(void) {
  check("control slow probes", 200, 1000, 0, BUFFER_UNAVAILABLE);
  check("bulk slow probes", 12, 1000, 0, BUFFER_UNAVAILABLE);
  check("immediate credit", 200, 1000, 1, BUFFER_AVAILABLE);
  check("transient starvation", 200, 1000, 4, BUFFER_AVAILABLE);
  check("fast probes", 200, 0, 0, BUFFER_UNAVAILABLE);
  check("single overlong probe", 200, 300000, 0, BUFFER_UNAVAILABLE);
  bus_error = 1;
  check("dead bus exits at threshold", 200, 1000, 0, BUFFER_UNAVAILABLE);
  if (probes != 32 || notifications != 1) return 2;
  check("outage reported once", 200, 1000, 0, BUFFER_UNAVAILABLE);
  if (post_attempts != 1 || notifications != 1) return 3;
  sdio_tx_unresponsive_reported = false; post_fails = 1;
  check("event queue full", 200, 1000, 0, BUFFER_UNAVAILABLE);
  if (sdio_tx_unresponsive_reported || post_attempts != 2) return 4;
  post_fails = 0;
  check("failed notification retried", 200, 1000, 0, BUFFER_UNAVAILABLE);
  if (!sdio_tx_unresponsive_reported || notifications != 2) return 5;
  return 0;
}
'''


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=ROOT / 'apps/orcsdr-tab5/build-sdio-credit-host')
    args = parser.parse_args()
    text = SOURCE.read_text()
    begin = text.index('/* Poll credits WITHOUT holding the bus lock')
    end = text.index('\n\t\tif (got != BUFFER_AVAILABLE)', begin)
    loop = text[begin:end]
    report_begin = text.index('/* ORCSDR-TAB5 (#106): report a dead link once per outage.', end)
    report_end = text.index('\n\t\t\tbreak;', report_begin)
    report = text[report_begin:report_end]
    work = args.build_dir / 'source'
    work.mkdir(parents=True, exist_ok=True)
    (work / 'credit.c').write_text(HARNESS.replace('/* LOOP */', loop).replace('/* REPORT */', report))
    (work / 'CMakeLists.txt').write_text('cmake_minimum_required(VERSION 3.20)\nproject(sdio_credit_host C)\nadd_executable(credit credit.c)\nenable_testing()\nadd_test(NAME credit_deadline COMMAND credit)\n')
    commands = [
        ['cmake', '-S', str(work), '-B', str(args.build_dir)],
        ['cmake', '--build', str(args.build_dir), '--config', 'Debug'],
        ['ctest', '--test-dir', str(args.build_dir), '-C', 'Debug', '--output-on-failure'],
    ]
    for command in commands:
        result = subprocess.run(command, timeout=120)
        if result.returncode:
            return result.returncode
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
