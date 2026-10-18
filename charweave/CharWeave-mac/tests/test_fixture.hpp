#pragma once
#define EffectMain CharWeaveEffectMain
#include "../src/CharWeave.mm"
#undef EffectMain
#include <iostream>
#include <stdexcept>

using namespace cwmac;
static std::map<PF_Handle, size_t> handles;
static std::vector<PF_ParamDef> registered;
static PF_Err TestAddParam(PF_ProgPtr, PF_ParamIndex, PF_ParamDefPtr def) {
  registered.push_back(*def); return PF_Err_NONE;
}
static PF_Handle TestNew(A_u_longlong size) {
  PF_Handle h = static_cast<PF_Handle>(std::malloc(sizeof(void *)));
  if (!h) return nullptr;
  *h = static_cast<char *>(std::calloc(1, static_cast<size_t>(size)));
  if (!*h) { std::free(h); return nullptr; }
  handles[h] = size;
  return h;
}
static void *TestLock(PF_Handle h) { return h ? *h : nullptr; }
static void TestUnlock(PF_Handle) {}
static void TestDispose(PF_Handle h) {
  if (!h) return;
  if (handles.erase(h) != 1) throw std::runtime_error("double dispose / foreign handle");
  std::free(*h); std::free(h);
}
static A_u_longlong TestSize(PF_Handle h) { return handles.at(h); }
static void Require(bool condition, const char *message) {
  if (!condition) throw std::runtime_error(message);
}

struct Fixture {
  static constexpr int W=240, H=140;
  PF_ParamDef defs[PARAM_COUNT]{};
  PF_ParamDef *params[PARAM_COUNT];
  PF_UtilCallbacks utils{};
  PF_InData in{};
  std::vector<PF_Pixel8> source;
  PF_EffectWorld src{};
  Fixture(): source(W*H) {
    for (int i=0;i<PARAM_COUNT;++i) params[i]=&defs[i];
    utils.host_new_handle=TestNew; utils.host_lock_handle=TestLock;
    utils.host_unlock_handle=TestUnlock; utils.host_dispose_handle=TestDispose;
    utils.host_get_handle_size=TestSize; utils.ansi.strcpy=std::strcpy;
    in.utils=&utils; in.time_scale=24; in.current_time=12;
    in.downsample_x={1,1}; in.downsample_y={1,1};
    in.width=W; in.height=H;
    for(int y=0;y<H;++y) for(int x=0;x<W;++x) {
      auto &p=source[y*W+x]; p.alpha=255;
      p.red=(x*255)/(W-1); p.green=(y*255)/(H-1); p.blue=180;
    }
    src.data=source.data();src.width=W;src.height=H;src.rowbytes=W*sizeof(PF_Pixel8);
    defs[0].u.ld=src;
    defs[P_PALETTE].u.pd.value=3;defs[P_TEXT_MODE].u.pd.value=1;
    Set(P_CELL_SIZE,10);Set(P_MAX_SIZE,110);Set(P_MIN_SIZE,25);
    Set(P_CONTRAST,1.2);Set(P_GAMMA,1);Set(P_HIGHLIGHT_START,.7);
    Set(P_INFLUENCE_RADIUS,85);Set(P_TRAVEL_DISTANCE,35);
    Set(P_FLUTTER_SPEED,2);Set(P_FLUTTER_AMOUNT,12);
    Set(P_DISPERSION_ROTATION,25);Set(P_ORGANIC,70);Set(P_TURBULENCE_SCALE,70);
    defs[P_RANDOM_SEED].u.sd.value=17;
    defs[P_DISPERSION_CENTER].u.td.x_value=120*65536;
    defs[P_DISPERSION_CENTER].u.td.y_value=70*65536;
    defs[P_LOW_COLOR].u.cd.value.red=20;defs[P_LOW_COLOR].u.cd.value.green=100;
    defs[P_LOW_COLOR].u.cd.value.blue=255;
    defs[P_HIGH_COLOR].u.cd.value.red=255;defs[P_HIGH_COLOR].u.cd.value.green=255;
    defs[P_HIGH_COLOR].u.cd.value.blue=255;
    defs[P_BACKGROUND].u.cd.value.red=4;defs[P_BACKGROUND].u.cd.value.green=4;
    defs[P_BACKGROUND].u.cd.value.blue=12;
    TextConfigRaw cfg;SetDefaultText(cfg);
    defs[P_TEXT_KEYFRAMES].u.arb_d.value=MakeTextHandle(&in,cfg);
  }
  ~Fixture() { TestDispose(defs[P_TEXT_KEYFRAMES].u.arb_d.value); }
  void Set(int id,float value) { defs[id].u.fs_d.value=value; }
  std::vector<PF_Pixel8> Render(const PF_EffectWorld *input=nullptr) {
    std::vector<PF_Pixel8> pixels(W*H);
    PF_EffectWorld dst{};dst.data=pixels.data();dst.width=W;dst.height=H;
    dst.rowbytes=W*sizeof(PF_Pixel8);
    Require(RenderTyped<PF_Pixel8,255>(&in,params,input?input:&src,&dst)==PF_Err_NONE,"render error");
    return pixels;
  }
};
static bool Same(const std::vector<PF_Pixel8> &a,const std::vector<PF_Pixel8> &b) {
  return a.size()==b.size() && std::memcmp(a.data(),b.data(),a.size()*sizeof(PF_Pixel8))==0;
}
