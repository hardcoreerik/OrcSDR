#include "lora_dashboard_host_fakes.hpp"
#include "lora_rx_layout.hpp"
int main(){
  for(const auto viewport:{orcsdr::audio_header::HeaderRect{0,0,1280,720},{72,72,424,1088},{72,72,1088,424},{0,0,800,480},{0,0,480,800}}){
    setup(viewport.x+viewport.w,viewport.y+viewport.h);
    auto snapshot=packet_profile();snapshot.node_count=10;snapshot.event_count=10;
    snapshot.native_decoder_ready=true;
    for(unsigned i=0;i<10;++i){snapshot.nodes[i].id=i+1;snapshot.nodes[i].latitude_e7=451234567;snapshot.nodes[i].longitude_e7=-1229876543;std::snprintf(snapshot.nodes[i].name,32,"Mesh node %u",i);snapshot.events[i].verified=true;snapshot.events[i].sender=i+1;snapshot.events[i].packet_id=i+20;snapshot.events[i].received_ms=100;std::snprintf(snapshot.events[i].text,112,"Packet %u readable message",i);}
    if(!lora::set_viewport(viewport)){std::fprintf(stderr,"invalid viewport %d,%d %dx%d\n",viewport.x,viewport.y,viewport.w,viewport.h);assert(false);}lora::enter(snapshot);
    for(unsigned view=0;view<5;++view){
      assert(lora::select_view(static_cast<lora::View>(view)));
      host_display::reset();lora::draw();
      const auto layout=lora::rx_layout::make(viewport,static_cast<lora::View>(view),true);
      assert(layout.valid);
      for(const auto& primitive:host_display::prims){
        if(primitive.kind!=Prim::text)continue;
        int x,y,w,h;if(!host_display::effective(primitive,&x,&y,&w,&h)){if(primitive.str.empty())continue;std::fprintf(stderr,"ineffective %u: %s\n",view,primitive.str.c_str());assert(false);}
        if(x<viewport.x||y<viewport.y||x+w>viewport.x+viewport.w||y+h>viewport.y+viewport.h){std::fprintf(stderr,"outside view%u: %s %d,%d %dx%d\n",view,primitive.str.c_str(),x,y,w,h);assert(false);}
        assert(h>=16);
      }
      for(int tab=0;tab<6;++tab){const auto r=layout.tab[tab];const auto action=lora::handle_touch(r.x+r.w/2,r.y+r.h/2);assert(action.kind==lora::ActionKind::select_view&&action.value==unsigned(tab));}
      if(view==unsigned(lora::View::nodes)||view==unsigned(lora::View::traffic)){assert(lora::page_items(1));assert(lora::page_items(-1));}
      if(view==unsigned(lora::View::map)){
        const auto before=lora::map_state();assert(before.ready);
        host_display::reset();assert(lora::map_control(lora::MapControl::zoom_in));assert(full_clears()==0);
        assert(lora::map_state().zoom==before.zoom+1);
        assert(lora::map_control(lora::MapControl::zoom_out));assert(lora::map_state().zoom==before.zoom);
        const auto r=layout.map;assert(lora::map_touch(r.x+r.w/2,r.y+r.h/2,true,true));
        assert(lora::map_touch(r.x+r.w/2+20,r.y+r.h/2+10,true,false));
        assert(lora::map_touch(r.x+r.w/2+20,r.y+r.h/2+10,false,false));
      }
      host_display::reset();lora::update(snapshot);assert(full_clears()==0);
    }
    assert(lora::select_view(lora::View::tx_lab));assert(lora::select_tx_page(1));
    assert(lora::select_view(lora::View::overview));
    snapshot.tx.readiness=tx::Readiness::absent;lora::update(snapshot);
    const auto layout=lora::rx_layout::make(viewport,lora::View::overview,false);
    assert(layout.tab_count==5);const auto r=layout.tab[4];assert(lora::handle_touch(r.x+r.w/2,r.y+r.h/2).value==4);
    assert(!lora::select_view(lora::View::tx_lab));
    lora::leave();
  }
  puts("LoRa shared production RX views/navigation/populated text bounds PASS");
}
