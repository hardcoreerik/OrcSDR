import hashlib
import importlib.util
import io
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("capture", ROOT / "tools/development_capture.py")
capture = importlib.util.module_from_spec(spec)
spec.loader.exec_module(capture)

class DevelopmentDiagnosticsTests(unittest.TestCase):
    def test_capture_is_bounded_and_only_queries_health(self):
        class Stream:
            writes = []
            def write(self, data): self.writes.append(data)
            def read(self, length): return b"panic evidence"
        stream, log = Stream(), io.BytesIO()
        result = capture.capture(stream, log, 300, 20, clock=lambda: 0)
        self.assertEqual(log.getvalue(), b"panic evidencepanic ")
        self.assertEqual(result, {"bytes": 20, "stop": "size_limit", "complete": False})
        self.assertEqual(stream.writes, [b"\n", b"RTL_HEALTH BUILD\nRTL_HEALTH\n"])

    def test_snapshot_retains_symbols_and_hash(self):
        with tempfile.TemporaryDirectory() as tmp:
            base = Path(tmp); build = base / "build"; out = base / "out"
            build.mkdir(); out.mkdir()
            (build / "app.elf").write_bytes(b"original symbols")
            (build / "config").write_text("test config")
            (build / "project_description.json").write_text(json.dumps({"app_elf": "app.elf", "config_file": str(build / "config")}))
            result = capture.snapshot_build(build, out)
            (build / "app.elf").write_bytes(b"new build")
            self.assertEqual((out / "app.elf").read_bytes(), b"original symbols")
            self.assertEqual(result["elf_sha256"], hashlib.sha256(b"original symbols").hexdigest())
            self.assertEqual((out / "sdkconfig").read_text(), "test config")

    def test_native_targets_preserve_crash_capture_contract(self):
        for app in ("orcsdr-tab5", "orcsdr-p4-hosyond-console"):
            base = ROOT / "apps" / app
            config = (base / "sdkconfig.defaults").read_text()
            for setting in ("CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH=y", "CONFIG_ESP_COREDUMP_DATA_FORMAT_ELF=y", "CONFIG_ESP_COREDUMP_CHECKSUM_SHA256=y", "CONFIG_ESP_COREDUMP_STACK_SIZE=4096"):
                self.assertIn(setting, config)
            self.assertIn("orcsdr_diagnostics", (base / "main/CMakeLists.txt").read_text())
            self.assertIn("data, coredump", (base / "partitions.csv").read_text())
        self.assertIn("espcoredump", (ROOT / "components/orcsdr_diagnostics/CMakeLists.txt").read_text())
        table = (ROOT / "apps/orcsdr-p4-hosyond-console/partitions.csv").read_text()
        rows = [[part.strip() for part in line.split(",")] for line in table.splitlines() if line and not line.startswith("#")]
        self.assertEqual([(r[0], r[3], r[4]) for r in rows[:3]], [("nvs", "0x9000", "0x6000"), ("phy_init", "0xf000", "0x1000"), ("factory", "0x10000", "0x700000")])
        self.assertEqual(rows[3][0:5], ["coredump", "data", "coredump", "0x710000", "256K"])

    @unittest.skipUnless(shutil.which("gcc"), "requires host gcc (run under WSL)")
    def test_actual_shared_firmware_commands(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp); (out / "freertos").mkdir()
            platform = r"""
#pragma once
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#define MALLOC_CAP_INTERNAL 1
#define MALLOC_CAP_DMA 2
#define MALLOC_CAP_8BIT 4
#define MALLOC_CAP_SPIRAM 8
typedef enum { ESP_RST_UNKNOWN,ESP_RST_POWERON,ESP_RST_EXT,ESP_RST_SW,ESP_RST_PANIC,ESP_RST_INT_WDT,ESP_RST_TASK_WDT,ESP_RST_WDT,ESP_RST_DEEPSLEEP,ESP_RST_BROWNOUT,ESP_RST_SDIO,ESP_RST_USB,ESP_RST_JTAG,ESP_RST_EFUSE,ESP_RST_PWR_GLITCH,ESP_RST_CPU_LOCKUP } esp_reset_reason_t;
typedef struct {char project_name[32],version[32],idf_ver[32];uint8_t app_elf_sha256[32];} esp_app_desc_t;
extern int checked,healthy;
static inline size_t heap_caps_get_free_size(unsigned caps){return 100+caps;}
static inline size_t heap_caps_get_minimum_free_size(unsigned caps){return 50+caps;}
static inline size_t heap_caps_get_largest_free_block(unsigned caps){return 70+caps;}
static inline bool heap_caps_check_integrity_all(bool print){++checked;return healthy;}
static inline unsigned esp_get_free_heap_size(void){return 1000;}
static inline unsigned esp_get_minimum_free_heap_size(void){return 900;}
static inline unsigned uxTaskGetNumberOfTasks(void){return 12;}
static inline unsigned uxTaskGetStackHighWaterMark(void* task){return 321;}
static inline esp_reset_reason_t esp_reset_reason(void){return ESP_RST_PANIC;}
static inline int64_t esp_timer_get_time(void){return 5000000;}
static inline const esp_app_desc_t* esp_app_get_description(void){static const esp_app_desc_t app={.project_name="test",.version="v1",.idf_ver="5.5.4",.app_elf_sha256={0xab}};return &app;}
"""
            (out / "platform.h").write_text(platform)
            for name in ("esp_app_desc.h", "esp_heap_caps.h", "esp_system.h", "esp_timer.h", "freertos/FreeRTOS.h", "freertos/task.h"):
                (out / name).write_text('#include "platform.h"\n')
            source = r"""
#include "orcsdr_diagnostics.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
int checked=0,healthy=1;
static char output[2048];
static void emit(const char* line){assert(strlen(output)+strlen(line)<sizeof(output));strcat(output,line);}
int main(int argc,char** argv){
 assert(argc==2);
 assert(!orcsdr_diagnostics_command("RTL_HEALTHY",false,emit));
 assert(!orcsdr_diagnostics_command(NULL,false,emit));
 assert(orcsdr_diagnostics_command("RTL_HEALTH",false,emit));
 assert(strstr(output,"reset_reason=4 reset_name=panic")&&strstr(output,"caller_stack_hwm=321")&&strstr(output,"uptime_ms=5000"));
 assert(checked==0);output[0]=0;
 assert(orcsdr_diagnostics_command("RTL_HEALTH HEAP_CHECK",false,emit));
 assert(strstr(output,"auth_required")&&checked==0);output[0]=0;
 assert(orcsdr_diagnostics_command("RTL_HEALTH HEAP_CHECK",true,emit));
 assert(strstr(output,"STARTED")&&strstr(output,"ok=1")&&checked==1);output[0]=0;healthy=0;
 assert(orcsdr_diagnostics_command("RTL_HEALTH HEAP_CHECK",true,emit));
 assert(strstr(output,"ok=0")&&checked==2);output[0]=0;
 assert(orcsdr_diagnostics_command("RTL_HEALTH BUILD",false,emit));
 assert(strstr(output,"elf_sha256=ab00000000000000000000000000000000000000000000000000000000000000")&&strstr(output,"coredump="));
 assert(strstr(output,argv[1]));
 output[0]=0;assert(orcsdr_diagnostics_command("RTL_HEALTH INVALID",true,emit));assert(strstr(output,"ERROR"));
 puts("shared health, build identity, authentication and heap result PASS");
}
"""
            (out / "test.c").write_text(source)
            for mode, definition in (("flash", "CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH"), ("uart", "CONFIG_ESP_COREDUMP_ENABLE_TO_UART"), ("disabled", "UNUSED")):
                (out / "sdkconfig.h").write_text(f"#define {definition} 1\n")
                subprocess.run(["gcc", "-std=c11", "-Wall", "-Wextra", "-Wno-unused-parameter", "-Werror", "-I"+str(out), "-I"+str(ROOT / "components/orcsdr_diagnostics/include"), str(out / "test.c"), str(ROOT / "components/orcsdr_diagnostics/orcsdr_diagnostics.c"), "-o", str(out / "test")], check=True)
                subprocess.run([str(out / "test"), "coredump=" + mode], check=True)

if __name__ == "__main__":
    unittest.main()
