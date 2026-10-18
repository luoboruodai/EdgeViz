/* Raster extraction from a normal AE layer checkout. Fixed 8x8, 64 slots.
 * This deliberately does not enumerate/render AEGP layers during PF_Cmd_RENDER.
 * The UI manager spatially isolates the layers before AE renders the library.
 */
static StyleTexel MixTexel(const StyleTexel &a, const StyleTexel &b, float t) {
  return {lerpf(a.r,b.r,t),lerpf(a.g,b.g,t),lerpf(a.b,b.b,t),lerpf(a.a,b.a,t)};
}
static StyleTexel StyleSample(const GlyphMask &mask, float x, float y) {
  if (mask.stylePixels.empty() || x<0 || y<0 || x>=mask.width-1 || y>=mask.height-1) return {};
  const int ix=static_cast<int>(std::floor(x)), iy=static_cast<int>(std::floor(y));
  auto at=[&](int a,int b){return mask.stylePixels[static_cast<size_t>(b*mask.width+a)];};
  return MixTexel(MixTexel(at(ix,iy),at(ix+1,iy),x-ix),
                  MixTexel(at(ix,iy+1),at(ix+1,iy+1),x-ix),y-iy);
}
template <typename Pixel, int Max> static StyleTexel StyleWorldPixel(const PF_EffectWorld *world,int x,int y) {
  const auto *row=reinterpret_cast<const Pixel *>(reinterpret_cast<const uint8_t *>(world->data)+
      static_cast<ptrdiff_t>(y)*world->rowbytes);
  const auto &p=row[x];
  const float a=clamp01(static_cast<float>(p.alpha)/Max);
  return {clampf(static_cast<float>(p.red)/Max,0,a),
          clampf(static_cast<float>(p.green)/Max,0,a),
          clampf(static_cast<float>(p.blue)/Max,0,a),a};
}
static StyleTexel StyleWorldPixel(const PF_EffectWorld *world,int x,int y) {
  return PF_WORLD_IS_DEEP(world) ? StyleWorldPixel<PF_Pixel16,32768>(world,x,y)
                               : StyleWorldPixel<PF_Pixel8,255>(world,x,y);
}
static StyleTexel StyleWorldSample(const PF_EffectWorld *world,float x,float y,
                                   int left,int top,int right,int bottom) {
  x=clampf(x,left,right);y=clampf(y,top,bottom);
  const int ix=static_cast<int>(std::floor(x)),iy=static_cast<int>(std::floor(y));
  const int nx=std::min(ix+1,right),ny=std::min(iy+1,bottom);
  return MixTexel(MixTexel(StyleWorldPixel(world,ix,iy),StyleWorldPixel(world,nx,iy),x-ix),
                  MixTexel(StyleWorldPixel(world,ix,ny),StyleWorldPixel(world,nx,ny),x-ix),y-iy);
}
static GlyphAtlas MakeStyleAtlas(const PF_EffectWorld *world,float size,bool densityOrder) {
  GlyphAtlas atlas;
  if(!world || !world->data || world->width<8 || world->height<8 ||
     world->width>8192 || world->height>8192) return atlas;
  const int pixelSize=PF_WORLD_IS_DEEP(world)?sizeof(PF_Pixel16):sizeof(PF_Pixel8);
  if(std::abs(static_cast<long long>(world->rowbytes)) < static_cast<long long>(world->width)*pixelSize)
    return atlas;
  const int target=std::max(1,static_cast<int>(std::lround(clampf(size,1,256))));
  for(int slot=0;slot<64;++slot) {
    const int col=slot%8,row=slot/8;
    const int x0=col*world->width/8,x1=(col+1)*world->width/8;
    const int y0=row*world->height/8,y1=(row+1)*world->height/8;
    int left=x1,top=y1,right=x0-1,bottom=y0-1;
    double ink=0;
    for(int y=y0;y<y1;++y) for(int x=x0;x<x1;++x) {
      const float a=StyleWorldPixel(world,x,y).a;ink+=a;
      if(a>1.0f/65536.0f) {left=std::min(left,x);right=std::max(right,x);top=std::min(top,y);bottom=std::max(bottom,y);}
    }
    if(right<left || bottom<top) continue; // Disabled / empty slots are not fake glyphs.
    const int sourceW=right-left+1,sourceH=bottom-top+1;
    const float factor=static_cast<float>(target)/std::max(sourceW,sourceH);
    const int contentW=std::max(1,static_cast<int>(std::lround(sourceW*factor)));
    const int contentH=std::max(1,static_cast<int>(std::lround(sourceH*factor)));
    GlyphMask mask;mask.width=contentW+4;mask.height=contentH+4;
    mask.styleSlot=slot;mask.density=static_cast<float>(ink/((x1-x0)*(y1-y0)));
    mask.alpha.resize(static_cast<size_t>(mask.width*mask.height),0);
    mask.stylePixels.resize(mask.alpha.size());
    for(int y=0;y<contentH;++y) for(int x=0;x<contentW;++x) {
      const float sx=left+(x+.5f)*sourceW/contentW-.5f;
      const float sy=top+(y+.5f)*sourceH/contentH-.5f;
      const StyleTexel p=StyleWorldSample(world,sx,sy,left,top,right,bottom);
      const size_t dst=static_cast<size_t>((y+2)*mask.width+x+2);
      mask.stylePixels[dst]=p;mask.alpha[dst]=static_cast<uint8_t>(std::lround(p.a*255));
    }
    atlas.tokens.emplace_back(1,static_cast<char16_t>(0xe000+slot));
    atlas.tokenToMask.push_back(atlas.masks.size());
    atlas.masks.push_back(std::move(mask));
  }
  if(densityOrder) std::stable_sort(atlas.tokenToMask.begin(),atlas.tokenToMask.end(),
      [&](size_t a,size_t b){return atlas.masks[a].density<atlas.masks[b].density;});
  return atlas;
}
