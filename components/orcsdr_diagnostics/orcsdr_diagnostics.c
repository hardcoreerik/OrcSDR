#include "orcsdr_diagnostics.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char* reset_name(esp_reset_reason_t reason) {
    switch(reason) {
    case ESP_RST_POWERON:return "power_on";
    case ESP_RST_EXT:return "external";
    case ESP_RST_SW:return "software";
    case ESP_RST_PANIC:return "panic";
    case ESP_RST_INT_WDT:return "interrupt_watchdog";
    case ESP_RST_TASK_WDT:return "task_watchdog";
    case ESP_RST_WDT:return "watchdog";
    case ESP_RST_DEEPSLEEP:return "deep_sleep";
    case ESP_RST_BROWNOUT:return "brownout";
    case ESP_RST_SDIO:return "sdio";
    case ESP_RST_USB:return "usb";
    case ESP_RST_JTAG:return "jtag";
    case ESP_RST_EFUSE:return "efuse";
    case ESP_RST_PWR_GLITCH:return "power_glitch";
    case ESP_RST_CPU_LOCKUP:return "cpu_lockup";
    default:return "other";
    }
}

bool orcsdr_diagnostics_command(const char* command, bool authenticated, void (*emit)(const char*)) {
    if(!command || !emit)return false;
    if(strcmp(command,"RTL_HEALTH") && strncmp(command,"RTL_HEALTH ",11))return false;
    char line[768];
    if(!strcmp(command,"RTL_HEALTH") || !strcmp(command,"RTL_HEALTH STATUS")) {
        const unsigned dma=MALLOC_CAP_INTERNAL|MALLOC_CAP_DMA;
        const unsigned internal=MALLOC_CAP_INTERNAL|MALLOC_CAP_8BIT;
        const esp_reset_reason_t reason=esp_reset_reason();
        // This is the calling task's watermark, not necessarily app_main's stack.
        const unsigned stack=(unsigned)uxTaskGetStackHighWaterMark(NULL);
        snprintf(line,sizeof(line),"RTL_HEALTH_STATUS uptime_ms=%llu free_heap=%u min_free_heap=%u "
            "dma_free=%u dma_min=%u dma_largest=%u tasks=%u main_stack_hwm=%u caller_stack_hwm=%u "
            "reset_reason=%d reset_name=%s internal_free=%u internal_min=%u internal_largest=%u "
            "psram_free=%u psram_largest=%u\n",
            (unsigned long long)(esp_timer_get_time()/1000), (unsigned)esp_get_free_heap_size(),
            (unsigned)esp_get_minimum_free_heap_size(), (unsigned)heap_caps_get_free_size(dma),
            (unsigned)heap_caps_get_minimum_free_size(dma), (unsigned)heap_caps_get_largest_free_block(dma),
            (unsigned)uxTaskGetNumberOfTasks(),stack,stack,(int)reason,reset_name(reason),
            (unsigned)heap_caps_get_free_size(internal),(unsigned)heap_caps_get_minimum_free_size(internal),
            (unsigned)heap_caps_get_largest_free_block(internal),
            (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM),(unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM));
        emit(line);return true;
    }
    if(!strcmp(command,"RTL_HEALTH BUILD")) {
        const esp_app_desc_t* app=esp_app_get_description();
        char sha[65];
        for(unsigned i=0;i<32;++i)snprintf(sha+2*i,3,"%02x",app->app_elf_sha256[i]);
#if CONFIG_ESP_COREDUMP_ENABLE_TO_FLASH
        const char* dump="flash";
#elif CONFIG_ESP_COREDUMP_ENABLE_TO_UART
        const char* dump="uart";
#else
        const char* dump="disabled";
#endif
        snprintf(line,sizeof(line),"RTL_HEALTH_BUILD project=%.32s version=%.32s idf=%.32s elf_sha256=%s coredump=%s\n",
            app->project_name,app->version,app->idf_ver,sha,dump);
        emit(line);return true;
    }
    if(!strcmp(command,"RTL_HEALTH HEAP_CHECK")) {
        if(!authenticated){emit("RTL_HEALTH_ERROR auth_required\n");return true;}
        emit("RTL_HEALTH_HEAP_CHECK_STARTED\n");
        const bool ok=heap_caps_check_integrity_all(true);
        emit(ok?"RTL_HEALTH_HEAP_CHECK ok=1\n":"RTL_HEALTH_HEAP_CHECK ok=0\n");
        return true;
    }
    emit("RTL_HEALTH_ERROR use STATUS|BUILD|HEAP_CHECK\n");return true;
}
