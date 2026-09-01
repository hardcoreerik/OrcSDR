#include "location_estimate.hpp"
#include <cJSON.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstring>
namespace orcsdr::location_estimate { namespace {
State g_state{}; portMUX_TYPE g_lock=portMUX_INITIALIZER_UNLOCKED; std::atomic_bool g_busy{false};
char g_query[64]{};
char g_last_query[64]{};
void set(const State& value) { portENTER_CRITICAL(&g_lock); g_state=value; portEXIT_CRITICAL(&g_lock); }
bool encode_query(const char* input, char* output, size_t capacity) {
  if (!input || !input[0] || !output || capacity == 0) return false;
  static constexpr char hex[] = "0123456789ABCDEF";
  size_t used = 0;
  for (const uint8_t* p = reinterpret_cast<const uint8_t*>(input); *p; ++p) {
    if (*p < 32 || *p == 127) return false;
    const bool plain = std::isalnum(*p) || *p == '-' || *p == '_' || *p == '.' || *p == '~';
    const size_t needed = plain ? 1 : 3;
    if (used + needed >= capacity) return false;
    if (plain) output[used++] = static_cast<char>(*p);
    else { output[used++] = '%'; output[used++] = hex[*p >> 4]; output[used++] = hex[*p & 15]; }
  }
  output[used] = '\0';
  return used != 0;
}
void worker(void*) { const bool address = g_query[0] != '\0'; State next{}; next.busy=true; strlcpy(next.message,address ? "Looking up address" : "Looking up IP area",sizeof(next.message)); set(next);
  char encoded[192]{}, url[320]{};
  bool ok = !address || encode_query(g_query, encoded, sizeof(encoded));
  if (address) snprintf(url, sizeof(url), "https://nominatim.openstreetmap.org/search?q=%s&format=jsonv2&limit=1", encoded);
  else strlcpy(url, "https://ipwho.is/", sizeof(url));
  char body[2048]{}; esp_http_client_config_t cfg{}; cfg.url=url; cfg.timeout_ms=12000; cfg.crt_bundle_attach=esp_crt_bundle_attach; cfg.user_agent="OrcSDR/0.2 (+https://github.com/hardcoreerik/OrcSDR)";
  esp_http_client_handle_t http=ok?esp_http_client_init(&cfg):nullptr; ok=ok && http && esp_http_client_open(http,0)==ESP_OK;
  if (ok) { const int64_t size=esp_http_client_fetch_headers(http); ok=size>=0 && (size==0 || size < (int64_t)sizeof(body)); size_t used=0; while(ok && used<sizeof(body)-1) { const int got=esp_http_client_read(http,body+used,sizeof(body)-1-used); if(got<0){ok=false;break;} if(got==0){ok=esp_http_client_is_complete_data_received(http);break;} used+=static_cast<size_t>(got); if(size>0 && used==static_cast<size_t>(size)) break; } body[used]='\0'; ok=ok && (size==0 ? esp_http_client_is_complete_data_received(http) : used==static_cast<size_t>(size)) && esp_http_client_get_status_code(http)==200; }
  if(http){esp_http_client_close(http);esp_http_client_cleanup(http);} next={};
  cJSON* root=ok?cJSON_Parse(body):nullptr;
  const cJSON* result=address && cJSON_IsArray(root)?cJSON_GetArrayItem(root,0):root;
  const cJSON* lat=result?cJSON_GetObjectItem(result,address?"lat":"latitude"):nullptr;
  const cJSON* lon=result?cJSON_GetObjectItem(result,address?"lon":"longitude"):nullptr;
  const double latitude=address && cJSON_IsString(lat)?strtod(lat->valuestring,nullptr):cJSON_IsNumber(lat)?lat->valuedouble:NAN;
  const double longitude=address && cJSON_IsString(lon)?strtod(lon->valuestring,nullptr):cJSON_IsNumber(lon)?lon->valuedouble:NAN;
  const cJSON* label=result?cJSON_GetObjectItem(result,address?"display_name":"city"):nullptr;
  if(result && std::isfinite(latitude) && std::isfinite(longitude) && fabs(latitude)<=90 && fabs(longitude)<=180) { next.ready=true; next.latitude_e7=(int32_t)llround(latitude*10000000.0); next.longitude_e7=(int32_t)llround(longitude*10000000.0); snprintf(next.label,sizeof(next.label),"%s: %s",address?"SEARCH":"IP AREA",cJSON_IsString(label)?label->valuestring:"UNKNOWN"); strlcpy(next.message,address?"Address found; confirm to save":"Approximate IP area; confirm to save",sizeof(next.message)); if(address)strlcpy(g_last_query,g_query,sizeof(g_last_query)); } else strlcpy(next.message,address?"Address not found":"IP area lookup failed",sizeof(next.message)); if(root)cJSON_Delete(root); set(next); g_busy.store(false,std::memory_order_release); vTaskDelete(nullptr); }
}
bool request(bool wifi_connected) { return request(nullptr, wifi_connected); }
bool request(const char* query, bool wifi_connected) { if(!wifi_connected){State error{};strlcpy(error.message,"Connect Wi-Fi before lookup",sizeof(error.message));set(error);return false;} if(query && query[0] && strcmp(query,g_last_query)==0 && state().ready)return true; bool expected=false; if(!g_busy.compare_exchange_strong(expected,true,std::memory_order_acq_rel))return false; strlcpy(g_query,query?query:"",sizeof(g_query)); State pending{}; pending.busy=true; strlcpy(pending.message,g_query[0]?"Address lookup queued":"IP area lookup queued",sizeof(pending.message)); set(pending); if(xTaskCreatePinnedToCoreWithCaps(worker,"location_lookup",8192,nullptr,3,nullptr,1,MALLOC_CAP_SPIRAM|MALLOC_CAP_8BIT)==pdPASS)return true; g_busy.store(false,std::memory_order_release); pending.busy=false; strlcpy(pending.message,"Location lookup unavailable",sizeof(pending.message)); set(pending); return false; }
State state(){State copy{};portENTER_CRITICAL(&g_lock);copy=g_state;portEXIT_CRITICAL(&g_lock);return copy;}
bool self_check(){char encoded[64]{};return encode_query("97401 Main St",encoded,sizeof(encoded))&&strcmp(encoded,"97401%20Main%20St")==0&&!encode_query("bad\nquery",encoded,sizeof(encoded));}
}
