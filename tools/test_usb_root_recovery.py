# SPDX-License-Identifier: MIT
"""Extract USB hub functions for the rejected-power-off regression."""
from pathlib import Path
import argparse
import re

parser = argparse.ArgumentParser()
parser.add_argument('--baseline', action='store_true')
parser.add_argument('--source', type=Path, required=True, help='USB component directory')
parser.add_argument('--output', type=Path, required=True)
args = parser.parse_args()
source = (args.source / 'hub.c').read_text()
check = args.output
check.mkdir(parents=True, exist_ok=True)

def extract(name):
    match = re.search(r'^(?:static void|esp_err_t) ' + re.escape(name) + r'\([^\n]*\)\n\{', source, re.M)
    if match is None:
        raise SystemExit(f'Function not found in hub.c: {name}')
    start = match.start()
    end = source.index('\n}', start) + 2
    return source[start:end]

mock = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define ESP_OK 0
#define ESP_ERR_INVALID_STATE 0x103
#define ESP_ERR_NOT_FOUND 0x105
#define ORCSDR_USB_NODE_FIX 1
#define ENABLE_USB_HUBS 0
#define portMAX_DELAY 0xffffffffu
#define HUB_DRIVER_ENTER_CRITICAL() (++critical)
#define HUB_DRIVER_EXIT_CRITICAL() (--critical)
#define HUB_DRIVER_CHECK_FROM_CRIT(cond, rc) if (!(cond)) { HUB_DRIVER_EXIT_CRITICAL(); return rc; }
#define ESP_LOGD(...) ((void)0)
#define ESP_LOGE(...) ((void)0)
#define ESP_ERROR_CHECK(call) assert((call) == ESP_OK)
#define PORT_REQ_DISABLE 1
#define PORT_REQ_RECOVER 2
#define HUB_DRIVER_ACTION_ROOT_EVENT 1
#define HUB_DRIVER_ACTION_ROOT_REQ 2

typedef int esp_err_t;
typedef int hcd_port_handle_t;
typedef int usb_speed_t;
typedef enum { ROOT_PORT_STATE_NOT_POWERED, ROOT_PORT_STATE_POWERED,
    ROOT_PORT_STATE_DISABLED, ROOT_PORT_STATE_ENABLED } root_port_state_t;
typedef enum { HCD_PORT_STATE_NOT_POWERED, HCD_PORT_STATE_DISCONNECTED,
    HCD_PORT_STATE_RECOVERY } hcd_port_state_t;
typedef enum { HCD_PORT_EVENT_NONE, HCD_PORT_EVENT_CONNECTION,
    HCD_PORT_EVENT_DISCONNECTION, HCD_PORT_EVENT_ERROR, HCD_PORT_EVENT_OVERCURRENT } hcd_port_event_t;
enum { HCD_PORT_CMD_POWER_OFF, HCD_PORT_CMD_POWER_ON, HCD_PORT_CMD_RESET, HCD_PORT_CMD_DISABLE };
typedef struct {
    struct { hcd_port_handle_t root_port_hdl; int root_port_lock; } constant;
    struct { root_port_state_t root_port_state; unsigned port_reqs; struct { unsigned actions; } flags; } dynamic;
} hub_driver_t;
static hub_driver_t hub;
static hub_driver_t *p_hub_driver_obj = &hub;
static unsigned critical, held, off_calls, on_calls, recover_calls, missing_node_calls;
static hcd_port_state_t hcd_state;
static hcd_port_event_t pending;
static bool interrupt_poweroff;
static void xSemaphoreTake(int lock, unsigned timeout) { (void)lock; (void)timeout; assert(!held && !critical); held++; }
static void xSemaphoreGive(int lock) { (void)lock; assert(held == 1 && !critical); held--; }
static hcd_port_state_t hcd_port_get_state(hcd_port_handle_t h) { (void)h; return hcd_state; }
static int hcd_port_command(hcd_port_handle_t h, int cmd) {
    (void)h;
    assert(!critical);
    if (cmd == HCD_PORT_CMD_POWER_OFF) {
        off_calls++;
        if (interrupt_poweroff) {
            pending = HCD_PORT_EVENT_DISCONNECTION;
            hub.dynamic.flags.actions |= HUB_DRIVER_ACTION_ROOT_EVENT;
            hcd_state = HCD_PORT_STATE_RECOVERY;
            interrupt_poweroff = false;
        }
        if (pending || hcd_state == HCD_PORT_STATE_NOT_POWERED) return ESP_ERR_INVALID_STATE;
        hcd_state = HCD_PORT_STATE_NOT_POWERED;
        return ESP_OK;
    }
    if (cmd == HCD_PORT_CMD_POWER_ON) {
        on_calls++;
        if (pending || hcd_state != HCD_PORT_STATE_NOT_POWERED) return ESP_ERR_INVALID_STATE;
        hcd_state = HCD_PORT_STATE_DISCONNECTED;
        return ESP_OK;
    }
    return ESP_ERR_INVALID_STATE;
}
static hcd_port_event_t hcd_port_handle_event(hcd_port_handle_t h) {
    (void)h; assert(held == 1 && !critical);
    hcd_port_event_t event = pending; pending = HCD_PORT_EVENT_NONE; return event;
}
static int hcd_port_recover(hcd_port_handle_t h) {
    (void)h; assert(held == 1 && hcd_state == HCD_PORT_STATE_RECOVERY && !pending);
    recover_calls++; hcd_state = HCD_PORT_STATE_NOT_POWERED; return ESP_OK;
}
static int hcd_port_get_speed(hcd_port_handle_t h, usb_speed_t *s) { (void)h; (void)s; return ESP_ERR_INVALID_STATE; }
static int dev_tree_node_new(void *p, int n, usb_speed_t s) { (void)p; (void)n; (void)s; return ESP_ERR_INVALID_STATE; }
static int dev_tree_node_dev_gone(void *p, int n) { (void)p; (void)n; missing_node_calls++; return ESP_ERR_NOT_FOUND; }
'''
main = r'''
int main(void) {
#ifdef _WIN32
    _set_error_mode(_OUT_TO_STDERR);
#endif
    hub.dynamic.root_port_state = ROOT_PORT_STATE_POWERED;
    hcd_state = HCD_PORT_STATE_DISCONNECTED;
    interrupt_poweroff = true;
    assert(hub_root_stop() == ESP_ERR_INVALID_STATE);
    assert(hub.dynamic.root_port_state == ROOT_PORT_STATE_POWERED && !held && off_calls == 1);
#ifndef BASELINE
    assert(hub_root_stop() == ESP_ERR_INVALID_STATE && off_calls == 1);
    assert(hub_process() == ESP_OK);
    assert(recover_calls == 1 && on_calls == 1 && missing_node_calls == 0);
    assert(hcd_state == HCD_PORT_STATE_DISCONNECTED && hub.dynamic.root_port_state == ROOT_PORT_STATE_POWERED);
    assert(hub_root_stop() == ESP_OK && off_calls == 2);
    assert(hub.dynamic.root_port_state == ROOT_PORT_STATE_NOT_POWERED && hcd_state == HCD_PORT_STATE_NOT_POWERED);
    assert(hub_root_stop() == ESP_OK && off_calls == 2);
    pending = HCD_PORT_EVENT_DISCONNECTION;
    hub.dynamic.flags.actions |= HUB_DRIVER_ACTION_ROOT_EVENT;
    hcd_state = HCD_PORT_STATE_RECOVERY;
    assert(hub_root_start() == ESP_ERR_INVALID_STATE && hub.dynamic.root_port_state == ROOT_PORT_STATE_NOT_POWERED);
    assert(hub_process() == ESP_OK && missing_node_calls == 1 && recover_calls == 2);
    assert(hcd_state == HCD_PORT_STATE_NOT_POWERED && hub.dynamic.root_port_state == ROOT_PORT_STATE_NOT_POWERED);
    assert(hub_root_start() == ESP_OK && hub.dynamic.root_port_state == ROOT_PORT_STATE_POWERED);
    hcd_state = HCD_PORT_STATE_NOT_POWERED;
    assert(hub_root_stop() == ESP_OK && off_calls == 2);
    assert(hub.dynamic.root_port_state == ROOT_PORT_STATE_NOT_POWERED && !held && !critical);
    puts("PASS: rejected power-off, event recovery, power cycle, already-off, missing-node recovery, re-enable");
#endif
    return 0;
}
'''
functions = ('hub_root_stop',) if args.baseline else ('root_port_handle_events', 'root_port_req', 'hub_root_start', 'hub_root_stop', 'hub_process')
(check / ('baseline.c' if args.baseline else 'candidate.c')).write_text(mock + '\n' + '\n\n'.join(map(extract, functions)) + main)
