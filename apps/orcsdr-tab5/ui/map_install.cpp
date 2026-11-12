#include "map_install.hpp"
#include "map_product.hpp"
#include "orcmap/pack_json.hpp"
#include <new>
namespace orcsdr::maps::install {
namespace {
constexpr std::string_view kRoot="/orcmaps/map-",kSuffix=".manifest.json";
struct Close {storage::File& file;~Close(){file.close();}};
Result state_error(delivery::Error error){return {Error::state,error};}
Result activation_error(activation::Error error){return {Error::activation,delivery::Error::none,transfer::Error::none,error};}
std::string directory(std::string_view path) {
  if(path.size()!=kRoot.size()+129+kSuffix.size()||path.substr(0,kRoot.size())!=kRoot||
      path.substr(path.size()-kSuffix.size())!=kSuffix)return {};
  auto dir=std::string("/orcmaps/.stage-")+std::string(path.substr(kRoot.size(),129));
  return activation::valid_directory(dir)?dir:std::string{};
}
bool read_pack(storage::FileSystem& fs,const std::string& path,catalog::Pack& pack) {
  auto file=fs.open(path.c_str(),FILE_READ);Close close{file};
  if(!file||file.size()>orcmap::kMaxPackManifestBytes)return false;
  pack.manifest_json.assign(file.size(),'\0');
  return file.read(reinterpret_cast<uint8_t*>(pack.manifest_json.data()),pack.manifest_json.size())==pack.manifest_json.size()&&
      file.close()&&orcmap::ParsePackManifestJson(pack.manifest_json,&pack.manifest)==orcmap::PackJsonError::kNone&&
      orcmap::ValidatePackManifest(pack.manifest)==orcmap::PackValidationError::kNone;
}
// Ownership may have committed even when the save reported an error. Close
// those journals before a later update can retire their visible manifests.
Result finalize_owned(storage::FileSystem& fs,const delivery::State& state) {
  for(const auto& owned:state.owned) {
    const auto dir=directory(owned.manifest);
    if(dir.empty())return {Error::input};
    const auto pending=fs.status((dir+"/activation.pending").c_str());
    if(pending==storage::EntryStatus::missing)continue;
    if(pending!=storage::EntryStatus::file)return activation_error(activation::Error::storage);
    const auto finished=activation::finalize(fs,dir);
    if(finished!=activation::Error::none)return activation_error(finished);
  }
  return {};
}
Result complete(storage::FileSystem& fs,delivery::Store& store,delivery::State& state,
                const catalog::Pack& pack,uint8_t* buffer,size_t size,const transfer::Hooks& hooks) {
  const auto target=activation::target_manifest(pack),dir=directory(target);
  if(dir.empty()||state.pending.product!=pack.product_id||state.pending.manifest!=target)return {Error::input};
  const auto recovered=activation::recover(fs,dir);
  if(recovered!=activation::Error::none)return activation_error(recovered);
  std::string previous;
  for(const auto& owned:state.owned)if(owned.product==pack.product_id)previous=owned.manifest;
  if(!previous.empty()) {
    const auto status=fs.status(previous.c_str());
    if(status==storage::EntryStatus::missing)previous.clear();
    else if(status!=storage::EntryStatus::file)return activation_error(activation::Error::storage);
    else {
      catalog::Pack old;
      if(!read_pack(fs,previous,old)||activation::target_manifest(old)!=previous)
        return activation_error(activation::Error::conflict);
    }
  }
  if(hooks.verifying)hooks.verifying(hooks.context);
  const auto activated=activation::activate(fs,pack,previous,buffer,size,hooks);
  if(activated!=activation::Error::none)return activation_error(activated);
  const auto recorded=delivery::record_install(state,pack.product_id,target,state.pending.select);
  if(recorded!=delivery::Error::none)return state_error(recorded);
  const auto saved=delivery::save(store,state);
  if(saved!=delivery::Error::none)return state_error(saved);
  const auto finished=activation::finalize(fs,dir);
  return finished==activation::Error::none?Result{}:activation_error(finished);
}
}
Result resume(storage::FileSystem& fs,delivery::Store& store,uint8_t* buffer,size_t size,const transfer::Hooks& hooks) {
  try {
    delivery::State state;const auto loaded=delivery::load(store,&state);
    if(loaded!=delivery::Error::none)return state_error(loaded);
    const auto finalized=finalize_owned(fs,state);
    if(finalized.error!=Error::none||state.pending.product.empty())return finalized;
    const auto dir=directory(state.pending.manifest);
    if(dir.empty())return {Error::input};
    {
      // The user (or a reformat) removed everything the interrupted install had
      // written. Nothing is left to finish, so drop the stale intent instead of
      // failing recovery on every boot. A fresh download can then start.
      const auto active=fs.status((dir+"/activation.manifest").c_str());
      const auto staged=fs.status((dir+"/pack.manifest.json").c_str());
      if(active==storage::EntryStatus::missing&&staged==storage::EntryStatus::missing) {
        state.pending={};
        const auto saved=delivery::save(store,state);
        return saved==delivery::Error::none?Result{}:state_error(saved);
      }
    }
    const auto recovered=activation::recover(fs,dir);
    if(recovered!=activation::Error::none)return activation_error(recovered);
    auto source=dir+"/activation.manifest";
    const auto status=fs.status(source.c_str());
    if(status==storage::EntryStatus::missing)source=dir+"/pack.manifest.json";
    else if(status!=storage::EntryStatus::file)return activation_error(activation::Error::storage);
    catalog::Pack pack;pack.product_id=state.pending.product;
    if(!read_pack(fs,source,pack)||activation::target_manifest(pack)!=state.pending.manifest)
      return activation_error(activation::Error::corrupt);
    return complete(fs,store,state,pack,buffer,size,hooks);
  }catch(const std::bad_alloc&){return {Error::allocation};}
}
Result run(storage::FileSystem& fs,delivery::Store& store,const catalog::Catalog& catalog,size_t index,size_t mirror,
           transfer::Stream& stream,uint64_t free,uint8_t* buffer,size_t size,bool select,uint64_t utc,const transfer::Hooks& hooks) {
  try {
    if(index>=catalog.packs.size()||!utc||(catalog.expires_at&&catalog.expires_at<=utc)||
        (catalog.generated_at>utc&&catalog.generated_at-utc>300))return {Error::input};
    delivery::State state;const auto loaded=delivery::load(store,&state);
    if(loaded!=delivery::Error::none)return state_error(loaded);
    const auto& pack=catalog.packs[index];
    if(!valid_product_id(pack.product_id)||!state.accepted_revision||state.accepted_revision!=catalog.revision||
        state.catalog_sha256!=catalog.sha256)return {Error::input};
    const auto target=activation::target_manifest(pack);
    if(target.empty())return {Error::input};
    if(!state.pending.product.empty()) {
      if(state.pending.product!=pack.product_id||state.pending.manifest!=target||state.pending.select!=select)return {Error::input};
      return resume(fs,store,buffer,size,hooks);
    }
    const auto finalized=finalize_owned(fs,state);
    if(finalized.error!=Error::none)return finalized;
    for(const auto& owned:state.owned)if(owned.product==pack.product_id&&owned.manifest==target) {
      const auto status=fs.status(target.c_str());
      if(status==storage::EntryStatus::error||status==storage::EntryStatus::directory)
        return activation_error(activation::Error::storage);
      const auto archive=target.substr(0,target.size()-14)+".pmtiles";
      const auto archive_status=fs.status(archive.c_str());
      if(archive_status==storage::EntryStatus::error||archive_status==storage::EntryStatus::directory)
        return activation_error(activation::Error::storage);
      if(status==storage::EntryStatus::file&&archive_status==storage::EntryStatus::file) {
        const auto planned=delivery::plan_install(state,pack.product_id,target,select);
        if(planned!=delivery::Error::none)return state_error(planned);
        const auto saved=delivery::save(store,state);
        if(saved!=delivery::Error::none)return state_error(saved);
        return complete(fs,store,state,pack,buffer,size,hooks);
      }
    }
    const auto staged=transfer::stage(fs,pack,mirror,stream,free,buffer,size,hooks);
    if(staged!=transfer::Error::none)return {Error::transfer,delivery::Error::none,staged};
    const auto planned=delivery::plan_install(state,pack.product_id,target,select);
    if(planned!=delivery::Error::none)return state_error(planned);
    const auto saved=delivery::save(store,state);
    if(saved!=delivery::Error::none)return state_error(saved);
    return complete(fs,store,state,pack,buffer,size,hooks);
  }catch(const std::bad_alloc&){return {Error::allocation};}
}
}
