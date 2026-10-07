#include "weather_report_format.hpp"
#include <cstdio>
#include <cstring>

namespace orcsdr::weather::report {
namespace {
bool csv_field(const char* input, char* output, size_t capacity, size_t* used){
  if(!input||!output||!used||*used+3>=capacity) return false;
  output[(*used)++]='"';
  for(const char* p=input;*p;++p){
    if(*used+3>=capacity) return false;
    if(*p=='"') output[(*used)++]='"';
    output[(*used)++]=*p;
  }
  output[(*used)++]='"'; output[*used]='\0'; return true;
}
}
const char* online_policy_key(OnlinePolicy p){ switch(p){case OnlinePolicy::disabled:return "disabled";case OnlinePolicy::manual:return "manual";case OnlinePolicy::automatic:return "automatic";}return "disabled"; }

bool escape_html(const char* input, char* output, size_t capacity) {
  if (!input || !output || capacity == 0) return false;
  size_t used = 0;
  for (const char* p = input; *p; ++p) {
    const char* replacement = nullptr;
    switch (*p) {
      case '&': replacement = "&amp;"; break;
      case '<': replacement = "&lt;"; break;
      case '>': replacement = "&gt;"; break;
      case '"': replacement = "&quot;"; break;
      case '\'': replacement = "&apos;"; break;
      default: break;
    }
    if (replacement) {
      const size_t length = std::strlen(replacement);
      if (used + length >= capacity) return false;
      std::memcpy(output + used, replacement, length);
      used += length;
    } else {
      if (used + 1 >= capacity) return false;
      output[used++] = *p;
    }
  }
  output[used] = '\0';
  return true;
}

bool encode_observation_csv(const Observation& o, char* out, size_t cap){
  if(!out||cap==0||!o.valid) return false;
  char prefix[192]{};
  const int n=std::snprintf(prefix,sizeof(prefix),"%s,%.6f,%s,%u,%u,",value_key(o.kind),static_cast<double>(o.value),source_key(o.meta.source),static_cast<unsigned>(o.meta.observed_utc),static_cast<unsigned>(o.meta.age_seconds));
  if(n<=0 || static_cast<size_t>(n)>=cap) return false;
  std::memcpy(out,prefix,static_cast<size_t>(n)); size_t used=static_cast<size_t>(n); out[used]='\0';
  return csv_field(o.source_label,out,cap,&used);
}

bool encode_session_json(const Session& s, char* out, size_t cap){
  if(!out||cap==0) return false;
  const int n=std::snprintf(out,cap,"{\"schema\":1,\"id\":\"%s\",\"utc_valid\":%s,\"started_utc\":%u,\"started_uptime_ms\":%u,\"online_policy\":\"%s\"}",s.id,s.utc_valid?"true":"false",static_cast<unsigned>(s.started_utc),static_cast<unsigned>(s.started_uptime_ms),online_policy_key(s.online_policy));
  return n>0 && static_cast<size_t>(n)<cap;
}

bool encode_history_jsonl(const Session& s, const char* type, char* out, size_t cap){
  if(!out||!type||cap==0) return false;
  const int n=std::snprintf(out,cap,"{\"schema\":1,\"id\":\"%s\",\"type\":\"%s\",\"utc_valid\":%s,\"started_utc\":%u,\"started_uptime_ms\":%u,\"online_policy\":\"%s\"}\n",s.id,type,s.utc_valid?"true":"false",static_cast<unsigned>(s.started_utc),static_cast<unsigned>(s.started_uptime_ms),online_policy_key(s.online_policy));
  return n>0 && static_cast<size_t>(n)<cap;
}
}  // namespace orcsdr::weather::report
