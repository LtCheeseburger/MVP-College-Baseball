#include "ui_style.h"
#include "overhaul.h"
#include "app/app.h"
#include "gpu/backend.h"
#include <cmath>
#include <algorithm>
#include <cstring>
#include <vector>
#include <filesystem>
namespace game {
namespace {
ImFont *fonts[7]{};
void NeutralGrain(ImDrawList* d, ImVec2 a, ImVec2 b, float opacity) {
  if(!gpu::g_ui_image || b.x<=a.x || b.y<=a.y)return;
  auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"menu-neutral-grain-v1.png"));
  if(!t)return;
  // Constant pixel density, clipped tiles: never stretch a tiny trim crop over
  // a large panel. Mirroring keeps edge transitions quiet at fractional scales.
  const float tile=1024.f*std::max(.75f,ImGui::GetIO().DisplaySize.y/1440.f);
  d->PushClipRect(a,b,true);
  for(int row=0;a.y+row*tile<b.y;++row)for(int col=0;a.x+col*tile<b.x;++col) {
    ImVec2 uv0{col%2?1.f:0.f,row%2?1.f:0.f},uv1{col%2?0.f:1.f,row%2?0.f:1.f};
    d->AddImage((ImTextureID)t,{a.x+col*tile,a.y+row*tile},{a.x+(col+1)*tile,a.y+(row+1)*tile},uv0,uv1,IM_COL32(255,255,255,int(opacity)));
  }
  d->PopClipRect();
}

}
void InitializeUi(void *) {
  ImGui::GetIO().Fonts->TexDesiredWidth=4096;
  auto root = overhaul::Root() / "fonts";
  const char *files[] = {"Inter_18pt-ExtraBoldItalic.ttf",
                         "BarlowCondensed-ExtraBold.ttf",
                         "Inter_18pt-Regular.ttf", "BarlowCondensed-Medium.ttf",
                         "BarlowCondensed-ExtraBoldItalic.ttf",
                         "BarlowCondensed-ExtraBold.ttf", "Inter_18pt-ExtraBoldItalic.ttf"};
  for (int i = 0; i < 7; i++) {
    std::vector<uint8_t> file;
    if (overhaul::Load(root / files[i], file)) {
      static const ImWchar glyphs[] = {0x20, 0xFF, 0x2000, 0x214F, 0};
      static const ImWchar displayGlyphs[] = {0x20, 0xFF, 0};
      static const ImWchar versusGlyphs[] = {'S', 'S', 'V', 'V', 0};
      // the atlas owns and frees its copy
      void *data = IM_ALLOC(file.size());
      std::memcpy(data, file.data(), file.size());
      fonts[i] = ImGui::GetIO().Fonts->AddFontFromMemoryTTF(
          data, int(file.size()), i == 5 ? 256.f : i == 6 ? 180.f : 96.f, nullptr,
          i == 5 ? displayGlyphs : i == 6 ? versusGlyphs : glyphs);
    }
  }
}
ImFont *Font(UiFont role) {
  auto f = fonts[int(role)];
  return f ? f : ImGui::GetFont();
}
void UiText(ImDrawList *draw, UiFont role, float size, ImVec2 pos, ImU32 color,
            const char *text, float maxWidth) {
  auto font = Font(role);
  if (maxWidth > 0) {
    float width = font->CalcTextSizeA(size, 1e6f, 0, text).x;
    if (width > maxWidth)
      size *= maxWidth / width;
  }
  draw->AddText(font, size, pos, color, text);
}
void UiBrandLogo(ImDrawList* draw, ImVec2 min, ImVec2 max, ImU32 tint) {
  if (!gpu::g_ui_image || max.x <= min.x || max.y <= min.y)
    return;
  const auto texture=gpu::g_ui_image(overhaul::Root()/"lg27.png");
  if (!texture)
    return;
  // Fit the supplied 2069 x 760 artwork inside each screen's existing logo box.
  const float scale=std::min((max.x-min.x)/2069.f,(max.y-min.y)/760.f);
  const ImVec2 size={2069*scale,760*scale};
  const ImVec2 start={(min.x+max.x-size.x)*.5f,(min.y+max.y-size.y)*.5f};
  draw->AddImage((ImTextureID)texture,start,{start.x+size.x,start.y+size.y},{0,0},{1,1},tint);
}
void UiBroadcastPanel(ImDrawList* d, ImVec2 a, ImVec2 b, float s, bool selected) {
  // Main-menu material system: charcoal, distressed texture, thin silver edges.
  d->AddRectFilled({a.x,a.y+4*s},{b.x,b.y+4*s},IM_COL32(0,0,0,85));
  d->AddRectFilled(a,b,IM_COL32(14,15,17,248),3*s);
  if(selected) {
    UiMenuHighlight(d,a,b,s);
    return;
  }
  d->PushClipRect(a,b,true);
  NeutralGrain(d,a,b,150);
  d->AddRectFilledMultiColor(a,b,IM_COL32(255,255,255,8),IM_COL32(255,255,255,3),IM_COL32(0,0,0,70),IM_COL32(0,0,0,45));
  d->PopClipRect();
  d->AddRect(a,b,IM_COL32(131,132,132,200),3*s,0,s);
  d->AddLine({a.x+3*s,a.y+s},{b.x-3*s,a.y+s},IM_COL32(233,231,225,125),s);
}
bool UiControllerGlyph(ImDrawList* d, ImVec2 a, ImVec2 b, const char* label, ImU32 tint) {
 if(!gpu::g_ui_image || !overhaul::Active() || !label)return false;
 auto tex=gpu::g_ui_image(overhaul::ControllerIcon(label));if(!tex)return false;
 const bool wide=std::strcmp(label,"A") && std::strcmp(label,"B") && std::strcmp(label,"X") && std::strcmp(label,"Y") && std::strcmp(label,"DPAD") && std::strcmp(label,"LS") && std::strcmp(label,"RS");
 const float aspect=wide?1.6f:1.f,w=b.x-a.x,h=b.y-a.y;
 float iw=std::min(w,h*aspect),ih=iw/aspect;ImVec2 start{a.x+(w-iw)*.5f,a.y+(h-ih)*.5f};
 d->AddImage((ImTextureID)tex,start,{start.x+iw,start.y+ih},{0,0},{1,1},tint);return true;
}
void UiTriggerBadge(ImDrawList* d, ImVec2 a, ImVec2 b, float s, const char* label) {
 if(UiControllerGlyph(d,a,b,label))return;
 UiBroadcastPanel(d,a,b,s);UiText(d,UiFont::Heading,20*s,{a.x+8*s,a.y+3*s},IM_COL32(232,239,244,255),label,b.x-a.x-12*s);
}
void UiControllerButton(ImDrawList* d, ImVec2 c, float r, float s, const char* label, ImU32 color) {
 if(UiControllerGlyph(d,{c.x-r,c.y-r},{c.x+r,c.y+r},label))return;
 d->AddCircleFilled({c.x,c.y+3*s},r+2*s,IM_COL32(0,0,0,175),40);
 d->AddCircleFilled(c,r,IM_COL32(31,38,43,255),40);d->AddCircleFilled(c,r-2*s,color,40);
 d->AddCircle(c,r-1*s,IM_COL32(255,255,255,130),40,s);
 d->AddCircleFilled({c.x-r*.2f,c.y-r*.28f},r*.48f,IM_COL32(255,255,255,24),32);
 const float fs=r*1.3f;auto f=Font(UiFont::Heading);auto size=f->CalcTextSizeA(fs,1e6f,0,label);
 UiText(d,UiFont::Heading,fs,{c.x-size.x*.5f,c.y-size.y*.5f},IM_COL32_WHITE,label);
}
void UiMenuHighlight(ImDrawList* draw, ImVec2 min, ImVec2 max, float scale) {
  draw->AddRectFilled(min,max,IM_COL32(137,13,27,255));
  if (gpu::g_ui_image)
    if (const auto texture=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"menu-selection-imagegen.png")))
      draw->AddImage((ImTextureID)texture,min,max,{.02f,.31f},{.83f,.64f});
  draw->AddRect(min,max,IM_COL32(242,240,232,255),0,0,scale);
  draw->AddRectFilled(min,{min.x+4*scale,max.y},IM_COL32(253,53,66,255));
}

void UiMenuBackdrop(ImDrawList* draw, ImVec2 screen, UiBackdrop style) {
  draw->AddRectFilled({0,0},screen,IM_COL32(10,11,13,255));
  if(screen.x<=0 || screen.y<=0)return;
  draw->PushClipRect({0,0},screen,true);
  NeutralGrain(draw,{0,0},screen,255);
  if(gpu::g_ui_image)if(auto t=gpu::g_ui_image(overhaul::UiAsset(overhaul::Root()/"menu-trim-imagegen.png"))) {
    const float width=screen.y*(2.f/3.f);
    // The original left streak is an accent only, not the fill material.
    draw->AddImage((ImTextureID)t,{0,0},{width,screen.y},{0,0},{1,1},IM_COL32(255,255,255,style==UiBackdrop::Charcoal?160:200));
  }
  draw->AddRectFilledMultiColor({0,0},screen,IM_COL32(0,0,0,10),IM_COL32(0,0,0,80),IM_COL32(0,0,0,165),IM_COL32(0,0,0,65));
  draw->PopClipRect();
}

void UiPrestigeStars(ImDrawList* draw, ImVec2 center, float radius, float spacing, int filled) {
  for (int star=0;star<6;++star,center.x+=spacing) {
    ImVec2 points[10];
    for (int i=0;i<10;++i) {
      const float angle=-1.57079633f+i*.62831853f;
      const float r=i%2 ? radius*.46f : radius;
      points[i]={center.x+std::cos(angle)*r,center.y+std::sin(angle)*r};
    }
    const bool gold=star<filled;
    for (int i=0;i<10;++i) {
      const auto fill=gold ? (i<3 || i>6 ? IM_COL32(255,213,94,255) : IM_COL32(240,162,36,255))
                           : IM_COL32(54,61,68,255);
      draw->AddTriangleFilled(center,points[i],points[(i+1)%10],fill);
    }
    draw->AddPolyline(points,10,gold?IM_COL32(255,222,119,255):IM_COL32(87,96,106,255),
                      ImDrawFlags_Closed,std::max(1.f,radius*.07f));
  }
}
} // namespace game
