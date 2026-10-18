#include "test_fixture.hpp"

int main() {
  try {
    Fixture f;
    auto plain=f.Render();Require(Same(plain,f.Render()),"render is not deterministic");
    size_t ink=0;for(const auto &p:plain) if(p.blue>12)++ink;
    Require(ink>200,"no visible glyphs");
    f.Set(P_DISPERSION_PROGRESS,80);auto disperse=f.Render();
    Require(!Same(plain,disperse),"dispersion does not alter pixels");
    f.Set(P_DISPERSION_PROGRESS,0);f.Set(P_SCATTER,1.5);f.Set(P_DRIFT_SPEED,0);
    f.in.current_time=0;auto still=f.Render();f.in.current_time=24;
    Require(Same(still,f.Render()),"zero drift animates unexpectedly");
    f.Set(P_DRIFT_SPEED,2);f.in.current_time=0;auto drift0=f.Render();
    f.in.current_time=24;Require(!Same(drift0,f.Render()),"drift is static at zero dispersion");
    std::cout<<"PASS deterministic / dispersion / drift\n";

    f.Set(P_SCATTER,0);f.Set(P_DRIFT_SPEED,0);f.in.current_time=12;
    std::vector<PF_Pixel16> src16(f.W*f.H),dst16(f.W*f.H);
    for(size_t i=0;i<src16.size();++i) {
      src16[i].alpha=32768;
      src16[i].red=std::lround(f.source[i].red*32768.0/255);
      src16[i].green=std::lround(f.source[i].green*32768.0/255);
      src16[i].blue=std::lround(f.source[i].blue*32768.0/255);
    }
    PF_EffectWorld srcDeep=f.src,dstDeep=f.src;
    srcDeep.data=reinterpret_cast<PF_PixelPtr>(src16.data());srcDeep.rowbytes=f.W*sizeof(PF_Pixel16);
    dstDeep.data=reinterpret_cast<PF_PixelPtr>(dst16.data());dstDeep.rowbytes=f.W*sizeof(PF_Pixel16);
    Require(RenderTyped<PF_Pixel16,32768>(&f.in,f.params,&srcDeep,&dstDeep)==PF_Err_NONE,"deep error");
    double error=0;for(size_t i=0;i<plain.size();++i) {
      error+=std::fabs(plain[i].red/255.0-dst16[i].red/32768.0);
      error+=std::fabs(plain[i].green/255.0-dst16[i].green/32768.0);
      error+=std::fabs(plain[i].blue/255.0-dst16[i].blue/32768.0);
    }
    error/=plain.size()*3;Require(error<.005,"8/16 bpc output disagrees");
    std::cout<<"PASS 8/16 bpc mean error="<<error<<"\n";

    f.defs[P_TRANSPARENT].u.bd.value=1;auto transparent=f.Render();
    size_t clear=0,partial=0;for(const auto &p:transparent) {
      Require(p.red<=p.alpha && p.green<=p.alpha && p.blue<=p.alpha,"invalid premultiplication");
      clear+=p.alpha==0;partial+=p.alpha>0 && p.alpha<255;
    }
    Require(clear>1000 && partial>100,"transparent output lacks alpha coverage");
    PF_Pixel8 half{128,128,0,0};PF_EffectWorld one{};
    one.data=&half;one.width=one.height=1;one.rowbytes=sizeof(half);
    Sample sample=ReadPixelAt<PF_Pixel8,255>(&one,0,0);
    Require(std::fabs(sample.color.r-1)<.0001 && std::fabs(sample.luma-.299)<.0001,"input double-premultiplied");
    PF_EffectWorld empty{};auto cleared=f.Render(&empty);
    for(const auto &p:cleared) Require(p.alpha==0 && p.red==0 && p.green==0 && p.blue==0,"empty input uninitialized");
    std::cout<<"PASS transparency / premultiplied input / empty input\n";

    f.in.downsample_x={1,2};f.in.downsample_y={1,2};
    RenderSettings settings=ReadSettings(&f.in,f.params);
    Require(settings.cellSize==5 && settings.centerX==60 && settings.centerY==35 && settings.travelDistance==17.5,"preview coordinates not scaled");
    f.defs[P_DISPERSION_CENTER].u.td.x_value=65536;
    Require(ReadSettings(&f.in,f.params).centerX==.5,"one-pixel center treated as normalized");
    f.in.downsample_x={1,1};f.in.downsample_y={1,1};
    std::cout<<"PASS half-resolution / small point coordinates\n";

    TextConfigRaw blank;SetDefaultText(blank);
    std::memset(blank.text,0,sizeof(blank.text));
    blank.text[0]=u'\r';blank.text[1]=u'\n';blank.text[2]=u'\t';
    Require(!ValidateTextConfig(blank),"line-break-only text accepted as visible glyphs");
    blank.text[3]=u'中';
    Require(ValidateTextConfig(blank),"visible BMP text mixed with whitespace rejected");
    std::memset(blank.font,0,sizeof(blank.font));blank.font[0]=u'\n';
    Require(!ValidateTextConfig(blank),"line-break-only font accepted");
    std::cout<<"PASS whitespace-only rejection / mixed visible BMP text\n";

    TextConfigRaw text;SetDefaultText(text);
    const uint16_t repeated[]={ '#','#','@',0x4f60,'#',0 };
    std::memset(text.text,0,sizeof(text.text));std::memcpy(text.text,repeated,sizeof(repeated));
    auto atlas=MakeAtlas(text,20);
    Require(atlas.tokens.size()==5 && atlas.masks.size()==3,"repeated tokens lost");
    Require(atlas.tokenToMask==std::vector<size_t>({0,0,1,2,0}),"token order lost");
    PF_Handle original=MakeTextHandle(&f.in,text),copy=nullptr;
    Require(CopyTextHandle(&f.in,original,&copy)==0 && original!=copy,"arbitrary data aliases");
    PF_ArbParamsExtra ex{};ex.id=P_TEXT_KEYFRAMES;ex.which_function=PF_Arbitrary_FLATTEN_FUNC;
    std::vector<uint8_t> flat(sizeof(text));
    ex.u.flatten_func_params.arbH=copy;ex.u.flatten_func_params.buf_sizeLu=flat.size();
    ex.u.flatten_func_params.flat_dataPV=flat.data();
    Require(TextArbitrary(&f.in,&ex)==0,"flatten failed");
    PF_Handle restored=nullptr;ex={};ex.id=P_TEXT_KEYFRAMES;
    ex.which_function=PF_Arbitrary_UNFLATTEN_FUNC;
    ex.u.unflatten_func_params.buf_sizeLu=flat.size();ex.u.unflatten_func_params.flat_dataPV=flat.data();
    ex.u.unflatten_func_params.arbPH=&restored;
    Require(TextArbitrary(&f.in,&ex)==0,"unflatten failed");
    TextConfigRaw read{};Require(ReadTextHandle(&f.in,restored,read) && std::memcmp(&read,&text,sizeof(text))==0,"Chinese roundtrip failed");
    flat[0]=0;PF_Handle bad=nullptr;ex.u.unflatten_func_params.arbPH=&bad;
    Require(TextArbitrary(&f.in,&ex)==PF_Err_BAD_CALLBACK_PARAM && bad==nullptr,"corrupt payload accepted");
    TestDispose(original);TestDispose(copy);TestDispose(restored);
    std::cout<<"PASS repeated text / Chinese TCW1 roundtrip / corrupt payload rejection\n";
    PF_OutData out{};GlobalSetup(&f.in,&out,f.params,nullptr);
    Require(out.out_flags==0x02000004 && out.my_version==0x00022402,"PiPL metadata mismatch");
    std::cout<<"PASS PiPL/global metadata consistency\n";
    f.in.inter.add_param=TestAddParam;
    Require(ParamsSetup(&f.in,&out,f.params,nullptr)==0,"parameter setup failed");
    Require(registered.size()==PARAM_COUNT-1 && out.num_params==PARAM_COUNT,"parameter count mismatch");
    for(size_t i=0;i<registered.size();++i) {
      const auto &p=registered[i];
      Require(p.uu.id==static_cast<A_long>(i+1),"disk parameter ID changed");
      if(i==P_EDIT_TEXT-1) Require((p.flags&PF_ParamFlag_SUPERVISE)!=0 && p.ui_flags==0,"edit button not supervised/visible");
      else if(i==P_CREATE_STYLES-1 || i==P_MANAGE_STYLES-1) Require(p.ui_flags==PF_PUI_DISABLED,"unstable native bridge not disabled");
      else if(i!=P_TEXT_KEYFRAMES-1) Require(p.flags==0 && p.ui_flags==0,"parameter inherited flags / lost keyframes");
    }
    TestDispose(registered[P_TEXT_KEYFRAMES-1].u.arb_d.dephault);
    std::cout<<"PASS all 39 parameter IDs / text button / disabled native creation bridge / clean flags\n";
  } catch(const std::exception &e) {std::cerr<<"FAIL: "<<e.what()<<"\n";return 2;}
  if(!handles.empty()) {std::cerr<<"FAIL: leaked handles="<<handles.size()<<"\n";return 3;}
  std::cout<<"ALL REGRESSIONS PASSED; no leaked test handles\n";
  return 0;
}
