#include "../ui/shortwave_dashboard_layout.hpp"
#include <cassert>

int main() {
  using namespace orcsdr::shortwave::layout;
  const Rect views[]={{0,0,1280,720},{0,0,800,480},{72,72,424,1088},{72,72,1088,424}};
  for(const auto v:views){
    const auto l=make_layout(v);assert(l.valid);
    for(const auto r:l.tab)assert(r.w>=44&&r.h>=44&&orcsdr::audio_header::header_inside(l.tabs,r));
    for(int i=0;i<11;++i)assert(l.quick[i].w>=44&&l.quick[i].h>=44&&orcsdr::audio_header::header_inside(l.content,l.quick[i]));
    for(int i=0;i<l.row_count;++i)for(const auto r:l.row_actions[i])assert((l.legacy||r.h>=44)&&r.w>=44&&orcsdr::audio_header::header_inside(l.content,r));
    assert(l.legacy||(l.zoom_minus.h>=44&&l.zoom_plus.h>=44));
    assert(l.hunt_previous.w>=44&&l.hunt_previous.h>=44&&l.hunt_next.w>=44&&l.hunt_next.h>=44);
    assert(orcsdr::audio_header::header_inside(v,l.spectrum));
    assert(orcsdr::audio_header::header_inside(v,l.waterfall));
    assert(l.row_count>=1&&l.row_count<=12);
    for(int i=0;i<l.row_count;++i)assert(orcsdr::audio_header::header_inside(l.content,l.rows[i]));
  }
  const auto legacy=make_layout(views[0]);
  assert(legacy.tab[0].x==4&&legacy.tab[4].x==1028);
  assert(legacy.frequency.x==24&&legacy.frequency.y==104&&legacy.spectrum.x==24&&legacy.spectrum.y==276);
  assert(make_layout({72,72,1088,424}).portrait==false);
  assert(make_layout({72,72,424,1088}).portrait==true);
}
