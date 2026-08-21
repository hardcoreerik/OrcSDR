#include "location_estimate.hpp"
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cmath>
#include <cstring>
namespace orcsdr::location_estimate { namespace {
State g_state{}; portMUX_TYPE g_lock=portMUX_INITIALIZER_UNLOCKED; std::atomic_bool g_busy{false};
void set(const State& value) { portENTER_CRITICAL(&g_lock); g_state=value; portEXIT_CRITICAL(&g_lock); }
void worker(void*) { State next{}; next.busy=true; strlcpy(next.message,"Looking up IP area",sizeof(next.message)); set(next);
  char body[1024]{}; esp_http_client_config_t cfg{}; cfg.url="https://ipwho.is/"; cfg.timeout_ms=12000; cfg.crt_bundle_attach=esp_crt_bundle_attach;
  esp_http_client_handle_t http=esp_http_client_init(&cfg); bool ok=http && esp_http_client_open(http,0)==ESP_OK;
  if (ok) { const int64_t size=esp_http_client_fetch_headers(http); ok=size>=0 && (size==0 || size < (int64_t)sizeof(body)); size_t used=0; while(ok && used<sizeof(body)-1) { const int got=esp_http_client_read(http,body+used,sizeof(body)-1-used); if(got<0){ok=false;break;} if(got==0){ok=esp_http_client_is_complete_data_received(http);break;} used+=static_cast<size_t>(got); if(size>0 && used==static_cast<size_t>(size)) break; } body[used]='\0'; ok=ok && (size==0 ? esp_http_client_is_complete_data_received(http) : used==static_cast<size_t>(size)) && esp_http_client_get_status_code(http)==200; }
  if(http){esp_http_client_close(http);esp_http_client_cleanup(http);} next={};
  cJSON* root=ok?cJSON_Parse(body):nullptr; const cJSON* lat=root?cJSON_GetObjectItem(root,"latitude"):nullptr; const cJSON* lon=root?cJSON_GetObjectItem(root,"longitude"):nullptr; const cJSON* city=root?cJSON_GetObjectItem(root,"city"):nullptr;
  if(root && cJSON_IsNumber(lat) && cJSON_IsNumber(lon) && fabs(lat->valuedouble)<=90 && fabs(lon->valuedouble)<=180) { next.ready=true; next.latitude_e7=(int32_t)llround(lat->valuedouble*10000000.0); next.longitude_e7=(int32_t)llround(lon->valuedouble*10000000.0); snprintf(next.label,sizeof(next.label),"IP AREA: %s",cJSON_IsString(city)?city->valuestring:"UNKNOWN"); strlcpy(next.message,"Approximate IP area; confirm to save",sizeof(next.message)); } else strlcpy(next.message,"IP area lookup failed",sizeof(next.message)); if(root)cJSON_Delete(root); set(next); g_busy.store(false,std::memory_order_release); vTaskDelete(nullptr); }
}
bool request(bool wifi_connected) { if(!wifi_connected)return false; bool expected=false; if(!g_busy.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false; State pending{}; pending.busy=true; strlcpy(pending.message,"IP area lookup queued",sizeof(pending.message)); set(pending); if(xTaskCreatePinnedToCore(worker,"ip_location",8192,nullptr,3,nullptr,1)==pdPASS)return true; g_busy.store(false,std::memory_order_release); pending.busy=false; strlcpy(pending.message,"IP area lookup unavailable",sizeof(pending.message)); set(pending); return false; }
State state(){State copy{};portENTER_CRITICAL(&g_lock);copy=g_state;portEXIT_CRITICAL(&g_lock);return copy;}
}
