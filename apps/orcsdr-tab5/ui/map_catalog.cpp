#include "map_catalog.hpp"
#include "map_product.hpp"
#include "map_download_response.hpp"
#include "orcmap/pack_json.hpp"
#include <cJSON.h>
#include <mbedtls/base64.h>
#include <mbedtls/ecp.h>
#include <mbedtls/pk.h>
#include <mbedtls/sha256.h>
#include <cmath>
#include <cstring>
#include <memory>
#include <new>

namespace orcsdr::maps::catalog {
namespace {
constexpr size_t kLimit=128*1024;
bool authenticate(std::string_view payload,std::string_view signature,
                  std::string_view pem,uint8_t digest[32]) {
  if(signature.empty()||signature.size()>128||pem.empty()||pem.size()>2048)return false;
  uint8_t der[80]{};size_t bytes=0;
  if(mbedtls_base64_decode(der,sizeof(der),&bytes,
       reinterpret_cast<const uint8_t*>(signature.data()),signature.size())||bytes<8)return false;
  if(mbedtls_sha256(reinterpret_cast<const uint8_t*>(payload.data()),payload.size(),digest,0))return false;
  const std::string key_text(pem);
  mbedtls_pk_context key;mbedtls_pk_init(&key);
  const int parsed=mbedtls_pk_parse_public_key(&key,
      reinterpret_cast<const uint8_t*>(key_text.c_str()),key_text.size()+1);
  const bool curve=parsed==0&&mbedtls_pk_can_do(&key,MBEDTLS_PK_ECDSA)&&
      mbedtls_ecp_keypair_get_group_id(mbedtls_pk_ec(key))==MBEDTLS_ECP_DP_SECP256R1;
  const bool verified=curve&&mbedtls_pk_verify(&key,MBEDTLS_MD_SHA256,digest,32,der,bytes)==0;
  mbedtls_pk_free(&key);return verified;
}
// Bound recursive DOM allocation/stack before invoking the JSON library.
// This is only a limit scan; cJSON still validates the complete grammar.
bool bounded(std::string_view text) {
  size_t depth=0,delimiters=0;bool quoted=false;
  for(size_t i=0;i<text.size();++i) {
    const unsigned char c=text[i];
    if(c==0)return false;
    if(quoted&&c=='\\') {
      if(++i>=text.size())return false;
      if(text[i]=='u'&&text.substr(i,5)=="u0000")return false;
      continue;
    }
    if(c=='"'){quoted=!quoted;continue;}
    if(quoted)continue;
    if(c=='{'||c=='['){if(++depth>16)return false;}
    if(c=='}'||c==']'){if(!depth)return false;--depth;}
    if(c=='{'||c=='['||c==':'||c==',')if(++delimiters>16384)return false;
  }
  return !quoted&&depth==0;
}
bool unique_tree(const cJSON* node,size_t& count) {
  if(++count>8192)return false;
  size_t fields=0;
  for(const cJSON* child=node->child;child;child=child->next) {
    if(cJSON_IsObject(node)) {
      if(++fields>128)return false;
      for(const cJSON* before=node->child;before!=child;before=before->next)
        if(!child->string||!before->string||std::strcmp(child->string,before->string)==0)return false;
    }
    if(!unique_tree(child,count))return false;
  }
  return true;
}
const cJSON* get(const cJSON* object,const char* key) {
  return cJSON_GetObjectItemCaseSensitive(object,key);
}
bool shape(const cJSON* object,std::initializer_list<const char*> keys) {
  if(!cJSON_IsObject(object)||cJSON_GetArraySize(object)!=static_cast<int>(keys.size()))return false;
  for(const char* key:keys)if(!get(object,key))return false;
  return true;
}
const char* text(const cJSON* value) {
  return cJSON_IsString(value)?value->valuestring:nullptr;
}
bool integer(const cJSON* value,uint64_t max,uint64_t& out) {
  if(!cJSON_IsNumber(value)||!std::isfinite(value->valuedouble)||value->valuedouble<1||
      value->valuedouble>static_cast<double>(max)||std::floor(value->valuedouble)!=value->valuedouble)return false;
  out=static_cast<uint64_t>(value->valuedouble);return true;
}
bool leap(int year){return year%4==0&&(year%100!=0||year%400==0);}
bool utc(const cJSON* value,uint64_t& out) {
  const char* s=text(value);if(!s||std::strlen(s)!=20)return false;
  if(s[4]!='-'||s[7]!='-'||s[10]!='T'||s[13]!=':'||s[16]!=':'||s[19]!='Z')return false;
  for(int i=0;i<19;++i)if(i!=4&&i!=7&&i!=10&&i!=13&&i!=16&&(s[i]<'0'||s[i]>'9'))return false;
  auto n=[s](int p){return (s[p]-'0')*10+s[p+1]-'0';};
  const int year=n(0)*100+n(2),month=n(5),day=n(8),hour=n(11),minute=n(14),second=n(17);
  if(year<1970||month<1||month>12||hour>23||minute>59||second>59)return false;
  const int lengths[]={31,28,31,30,31,30,31,31,30,31,30,31};
  if(day<1||day>lengths[month-1]+(month==2&&leap(year)?1:0))return false;
  uint64_t days=0;
  for(int y=1970;y<year;++y)days+=leap(y)?366:365;
  for(int m=1;m<month;++m)days+=lengths[m-1]+(m==2&&leap(year)?1:0);
  out=((days+day-1)*24+hour)*3600+minute*60+second;return true;
}
bool url(std::string_view value,std::string_view digest) {
  if(value.size()>512||value.substr(0,8)!="https://")return false;
  const size_t slash=value.find('/',8);if(slash==std::string_view::npos||slash==8)return false;
  auto host=value.substr(8,slash-8);
  if(host.size()>4&&host.substr(host.size()-4)==":443")host.remove_suffix(4);
  if(host.empty()||host.size()>253)return false;
  size_t label=0;
  for(size_t i=0;i<host.size();++i) {
    const char c=host[i];
    if(c=='.'){if(!label||host[i-1]=='-')return false;label=0;continue;}
    if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='-')||
        (!label&&c=='-')||++label>63)return false;
  }
  if(!label||host.back()=='-')return false;
  auto path=value.substr(slash);
  for(char c:path)if(!((c>='A'&&c<='Z')||(c>='a'&&c<='z')||(c>='0'&&c<='9')||
      c=='/'||c=='-'||c=='.'||c=='_'||c=='~'))return false;
  if(path.find("/../")!=std::string_view::npos||path.find("/./")!=std::string_view::npos||
      path.size()<8||path.substr(path.size()-8)!=".pmtiles")return false;
  const std::string segment="/"+std::string(digest)+"/";
  return path.find(segment)!=std::string_view::npos;
}
Error parse(std::string_view payload,const Policy& policy,const uint8_t digest[32],Catalog& out) {
  if(!bounded(payload))return Error::json;
  const char* end=nullptr;
  std::unique_ptr<cJSON,decltype(&cJSON_Delete)> root(
      cJSON_ParseWithLengthOpts(payload.data(),payload.size(),&end,false),cJSON_Delete);
  if(!root)return Error::json;
  while(end<payload.data()+payload.size()&&(*end==' '||*end=='\n'||*end=='\r'||*end=='\t'))++end;
  size_t count=0;
  if(end!=payload.data()+payload.size()||!unique_tree(root.get(),count))return Error::json;
  if(!shape(root.get(),{"schema","revision","generated_at","expires_at","packs"}))return Error::schema;
  const char* schema=text(get(root.get(),"schema"));
  if(!schema||std::strcmp(schema,"orcmaps-catalog-v1"))return Error::schema;
  if(!integer(get(root.get(),"revision"),9007199254740991ULL,out.revision))return Error::revision;
  const auto* expiry=get(root.get(),"expires_at");
  if(!utc(get(root.get(),"generated_at"),out.generated_at))return Error::schema;
  if(cJSON_IsNull(expiry))out.expires_at=0; // Explicit signed non-expiry.
  else if(!utc(expiry,out.expires_at)||out.expires_at<=out.generated_at)return Error::schema;
  if(!policy.clock_trusted||!policy.utc_now)return Error::clock;
  if(out.expires_at&&out.expires_at<=policy.utc_now)return Error::expired;
  if(out.generated_at>policy.utc_now&&out.generated_at-policy.utc_now>300)return Error::future;
  constexpr char hex[]="0123456789abcdef";
  for(size_t i=0;i<32;++i){out.sha256+=hex[digest[i]>>4];out.sha256+=hex[digest[i]&15];}
  if(out.revision<policy.accepted_revision||(out.revision==policy.accepted_revision&&
      out.sha256!=policy.accepted_sha256))return Error::rollback;
  const cJSON* packs=get(root.get(),"packs");
  const int size=cJSON_GetArraySize(packs);
  if(!cJSON_IsArray(packs)||size<1||size>static_cast<int>(kMaximumPacks))return Error::schema;
  out.packs.reserve(size);
  for(const cJSON* entry=packs->child;entry;entry=entry->next) {
    if(!shape(entry,{"product_id","manifest","archive"}))return Error::schema;
    const char* product=text(get(entry,"product_id"));
    if(!product||!valid_product_id(product))return Error::identity;
    for(const auto& previous:out.packs)if(previous.product_id==product)return Error::identity;
    const auto* archive=get(entry,"archive");
    if(!shape(archive,{"urls","bytes","sha256"}))return Error::schema;
    const auto* manifest=get(entry,"manifest");if(!cJSON_IsObject(manifest))return Error::manifest;
    std::unique_ptr<char,decltype(&cJSON_free)> serialized(cJSON_PrintUnformatted(manifest),cJSON_free);
    if(!serialized)return Error::allocation;
    Pack pack;pack.product_id=product;pack.manifest_json=serialized.get();
    if(orcmap::ParsePackManifestJson(pack.manifest_json,&pack.manifest)!=orcmap::PackJsonError::kNone||
        orcmap::ValidatePackManifest(pack.manifest)!=orcmap::PackValidationError::kNone||
        pack.manifest.pack_id.size()>255)return Error::manifest;
    uint64_t bytes=0;const char* hash=text(get(archive,"sha256"));
    if(!integer(get(archive,"bytes"),UINT32_MAX,bytes)||bytes!=pack.manifest.size_bytes||
        !hash||!download::detail::digest(hash)||pack.manifest.output_sha256!=hash)return Error::identity;
    for(const auto& previous:out.packs)if(previous.manifest.pack_id==pack.manifest.pack_id)return Error::identity;
    const auto* urls=get(archive,"urls");
    if(!cJSON_IsArray(urls)||cJSON_GetArraySize(urls)<1||cJSON_GetArraySize(urls)>4)return Error::url;
    for(const auto* item=urls->child;item;item=item->next) {
      const char* value=text(item);if(!value||!url(value,hash))return Error::url;
      for(const auto& previous:pack.urls)if(previous==value)return Error::url;
      pack.urls.emplace_back(value);
    }
    out.packs.push_back(std::move(pack));
  }
  return Error::none;
}
} // namespace
Error read_signed(std::string_view payload,std::string_view signature,std::string_view key,
                  const Policy& policy,Catalog* output) noexcept {
  if(!output||payload.empty()||payload.size()>kLimit)return Error::limit;
  try {
    uint8_t digest[32]{};if(!authenticate(payload,signature,key,digest))return Error::signature;
    Catalog next;const auto error=parse(payload,policy,digest,next);
    if(error==Error::none)*output=std::move(next);
    return error;
  }catch(const std::bad_alloc&){return Error::allocation;}
}
const char* error_name(Error error) {
  switch(error) {
#define NAME(e) case Error::e:return #e
    NAME(none);NAME(limit);NAME(signature);NAME(json);NAME(schema);NAME(revision);
    NAME(clock);NAME(expired);NAME(future);NAME(rollback);NAME(manifest);NAME(identity);NAME(url);NAME(allocation);
#undef NAME
  }
  return "unknown";
}
} // namespace orcsdr::maps::catalog
