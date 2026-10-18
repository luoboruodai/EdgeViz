#include "test_fixture.hpp"
#include <fstream>
static PF_EffectWorld checkedStyle{};
static int checkoutCount=0,checkinCount=0;
static PF_Err checkoutResult=0;
static PF_Err Checkout(PF_ProgPtr,PF_ParamIndex index,A_long time,A_long step,A_u_long scale,PF_ParamDef *param) {
  Require(index==P_STYLE_LAYER && time==12 && step==1 && scale==24,"checkout dependency/time wrong");
  ++checkoutCount;
  if(checkoutResult) return checkoutResult;
  param->param_type=PF_Param_LAYER;param->u.ld=checkedStyle;return 0;
}
static PF_Err Checkin(PF_ProgPtr,PF_ParamDef *) {++checkinCount;return 0;}
static PF_Err Abort(PF_ProgPtr) {return 1;}
static void SavePPM(const char *path,const std::vector<PF_Pixel8> &pixels,int w,int h) {
  std::ofstream stream(path,std::ios::binary);stream<<"P6\n"<<w<<" "<<h<<"\n255\n";
  for(const auto &p:pixels) {stream.put(p.red);stream.put(p.green);stream.put(p.blue);}
}
#ifndef CW_STYLE_DECLARATIONS_ONLY
int main() {
  try {
    Fixture f;f.in.time_step=1;
    constexpr int W=256,H=256,STRIDE=W+5;
    std::vector<PF_Pixel8> pixels(STRIDE*H);
    auto set=[&](int x,int y,int r,int g,int b,int a=255) {
      auto &p=pixels[y*STRIDE+x];p.alpha=a;p.red=r*a/255;p.green=g*a/255;p.blue=b*a/255;
    };
    // Slot 0: a thin shape. Slot 1: colored circle. Slot 8: wide multicolor image.
    for(int y=7;y<25;++y)for(int x=14;x<18;++x)set(x,y,255,255,255);
    for(int y=3;y<29;++y)for(int x=35;x<61;++x)
      if((x-48)*(x-48)+(y-16)*(y-16)<144) set(x,y,20,100,255);
    for(int y=43;y<53;++y)for(int x=2;x<30;++x) set(x,y,x<16?255:0,x<16?0:255,0,128);
    for(int y=234;y<246;++y)for(int x=234;x<246;++x)set(x,y,250,20,200);
    PF_EffectWorld world{};world.data=pixels.data();world.width=W;world.height=H;
    world.rowbytes=STRIDE*sizeof(PF_Pixel8);
    checkedStyle=world;
    auto atlas=MakeStyleAtlas(&world,20,false);
    Require(atlas.masks.size()==4 && atlas.tokens.size()==4,"empty slots / style count wrong");
    Require(atlas.masks[0].styleSlot==0 && atlas.masks[1].styleSlot==1 && atlas.masks[2].styleSlot==8 && atlas.masks[3].styleSlot==63,"layer order not retained");
    Require(atlas.masks[2].width>atlas.masks[2].height,"wide image distorted to square");
    Require(atlas.masks[0].width<atlas.masks[0].height,"thin image aspect lost");
    auto sorted=MakeStyleAtlas(&world,20,true);
    for(size_t i=1;i<sorted.tokenToMask.size();++i)
      Require(sorted.masks[sorted.tokenToMask[i-1]].density<=sorted.masks[sorted.tokenToMask[i]].density,"brightness density order wrong");
    auto texel=StyleSample(atlas.masks[2],atlas.masks[2].width*.25f,atlas.masks[2].height*.5f);
    Require(texel.a>.45 && texel.a<.55 && std::fabs(texel.r-texel.a)<.001 && texel.g==0,"image RGBA double-premultiplied");
    Require(StyleSample(atlas.masks[2],0,0).a==0,"transparent crop padding missing");
    Require(MakeStyleAtlas(nullptr,20,false).masks.empty(),"null atlas not empty");
    std::cout<<"PASS 64-slot scan / empty removal / density ordering / trim / aspect / RGBA\n";

    std::vector<PF_Pixel8> dstPixels(f.W*f.H);
    PF_EffectWorld dst=f.src;dst.data=dstPixels.data();
    f.defs[P_GLYPH_SOURCE].u.pd.value=2;f.defs[P_STYLE_COLOR].u.pd.value=2;
    f.defs[P_TEXT_MODE].u.pd.value=2;
    f.defs[P_TRANSPARENT].u.bd.value=1;f.Set(P_CELL_SIZE,16);f.Set(P_MAX_SIZE,100);f.Set(P_MIN_SIZE,100);
    f.in.inter.checkout_param=Checkout;f.in.inter.checkin_param=Checkin;
    PF_OutData out{};
    Require(RenderFrame(&f.in,&out,f.params,&dst)==0,"style render failed");
    Require(checkoutCount==1 && checkinCount==1,"unbalanced style checkout");
    auto colored=dstPixels;
    size_t blue=0,green=0,pink=0,partial=0;
    for(const auto &p:colored) {
      blue+=p.blue>p.red+20 && p.blue>p.green+20;
      green+=p.green>p.red+20 && p.green>p.blue+20;
      pink+=p.red>p.green+20 && p.blue>p.green+20;
      partial+=p.alpha>0 && p.alpha<255;
      Require(p.red<=p.alpha && p.green<=p.alpha && p.blue<=p.alpha,"style output not premultiplied");
    }
    std::cout<<"style pixels blue="<<blue<<" green="<<green<<" pink="<<pink<<" partial="<<partial<<"\n";
    Require(blue>100 && green>100 && pink>100 && partial>100,"multiple source colors / alpha lost");
    SavePPM("tests/style-library/layer-colors.ppm",colored,f.W,f.H);
    f.defs[P_STYLE_COLOR].u.pd.value=1;
    f.defs[P_LOW_COLOR].u.cd.value={255,255,0,0};
    f.defs[P_HIGH_COLOR].u.cd.value={255,255,0,0};
    Require(RenderFrame(&f.in,&out,f.params,&dst)==0,"tinted styles failed");
    Require(!Same(colored,dstPixels),"palette toggle does not change style pixels");
    for(const auto &p:dstPixels) Require(p.green==0 && p.blue==0,"tinted style kept original colors");
    SavePPM("tests/style-library/palette-colors.ppm",dstPixels,f.W,f.H);
    f.defs[P_STYLE_COLOR].u.pd.value=2;f.Set(P_DISPERSION_PROGRESS,80);
    Require(RenderFrame(&f.in,&out,f.params,&dst)==0 && !Same(colored,dstPixels),"styles do not disperse");
    SavePPM("tests/style-library/layer-dispersion.ppm",dstPixels,f.W,f.H);
    f.Set(P_DISPERSION_PROGRESS,0);
    std::cout<<"PASS repeat mode / keep layer colors / recolor / alpha / dispersion\n";

    std::vector<PF_Pixel16> library16(STRIDE*H),source16(f.W*f.H),output16(f.W*f.H);
    for(size_t i=0;i<pixels.size();++i) {
      auto &d=library16[i];const auto &p=pixels[i];
      d.alpha=std::lround(p.alpha*32768.0/255);d.red=std::lround(p.red*32768.0/255);
      d.green=std::lround(p.green*32768.0/255);d.blue=std::lround(p.blue*32768.0/255);
    }
    for(size_t i=0;i<source16.size();++i) {
      auto &d=source16[i];const auto &p=f.source[i];d.alpha=32768;
      d.red=std::lround(p.red*32768.0/255);d.green=std::lround(p.green*32768.0/255);d.blue=std::lround(p.blue*32768.0/255);
    }
    checkedStyle.data=reinterpret_cast<PF_PixelPtr>(library16.data());checkedStyle.world_flags=PF_WorldFlag_DEEP;
    checkedStyle.rowbytes=STRIDE*sizeof(PF_Pixel16);
    f.defs[0].u.ld.data=reinterpret_cast<PF_PixelPtr>(source16.data());f.defs[0].u.ld.rowbytes=f.W*sizeof(PF_Pixel16);
    f.defs[0].u.ld.world_flags=PF_WorldFlag_DEEP;
    dst.data=reinterpret_cast<PF_PixelPtr>(output16.data());dst.rowbytes=f.W*sizeof(PF_Pixel16);dst.world_flags=PF_WorldFlag_DEEP;
    Require(RenderFrame(&f.in,&out,f.params,&dst)==0,"16 bpc style render failed");
    double diff=0;
    for(size_t i=0;i<colored.size();++i) {
      diff+=std::fabs(colored[i].red/255.0-output16[i].red/32768.0);
      diff+=std::fabs(colored[i].green/255.0-output16[i].green/32768.0);
      diff+=std::fabs(colored[i].blue/255.0-output16[i].blue/32768.0);
    }
    diff/=colored.size()*3;
    Require(diff<.004,"8 / 16 bpc layer colors disagree");
    std::cout<<"PASS 16 bpc RGBA styles; mean normalized channel error="<<diff<<"\n";
    f.defs[0].u.ld=f.src;dst=f.src;dst.data=dstPixels.data();checkedStyle=world;

    int before=checkinCount;
    f.in.inter.abort=Abort;
    Require(RenderFrame(&f.in,&out,f.params,&dst)==PF_Interrupt_CANCEL,"abort ignored");
    Require(checkinCount==before+1,"style checkout leaked after abort");f.in.inter.abort=nullptr;
    before=checkinCount;checkoutResult=PF_Err_OUT_OF_MEMORY;
    Require(RenderFrame(&f.in,&out,f.params,&dst)==PF_Err_OUT_OF_MEMORY && checkinCount==before,"failed checkout was checked in");
    checkoutResult=0;checkedStyle={};before=checkinCount;
    Require(RenderFrame(&f.in,&out,f.params,&dst)==PF_Err_BAD_CALLBACK_PARAM && checkinCount==before+1 && out.return_msg[0],"missing library not reported / leaked");
    f.defs[P_GLYPH_SOURCE].u.pd.value=1;before=checkoutCount;
    Require(RenderFrame(&f.in,&out,f.params,&dst)==0 && checkoutCount==before,"Custom text depends on style layer");
    std::cout<<"PASS checkout balance / abort / missing source / legacy Custom text isolation\n";
  } catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 2;}
  Require(handles.empty(),"test host handles leaked");
  std::cout<<"ALL LAYER STYLE TESTS PASSED (standalone; not an AE UI test)\n";
}

#endif
