// Execute the production glyph implementation against a deterministic GDI model.
// This checks lifetime, fallback and DIB synchronization, not Windows/AE integration.
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using BOOL=int; using DWORD=uint32_t; using WORD=uint16_t;
constexpr BOOL FALSE=0, TRUE=1;
constexpr int FW_NORMAL=400, DEFAULT_CHARSET=1, OUT_TT_ONLY_PRECIS=7,
    CLIP_DEFAULT_PRECIS=0, ANTIALIASED_QUALITY=4, DEFAULT_PITCH=0,
    FF_DONTCARE=0, BI_RGB=0, DIB_RGB_COLORS=0, TRANSPARENT=1;
constexpr DWORD GDI_ERROR=0xffffffffu, GGI_MARK_NONEXISTING_GLYPHS=1;
#define RGB(r,g,b) ((DWORD)(r)|((DWORD)(g)<<8)|((DWORD)(b)<<16))
struct SIZE {int32_t cx,cy;};
struct TEXTMETRICW {int32_t tmHeight;};
struct RECT {int32_t left,top,right,bottom;};
struct BITMAPINFOHEADER {
  DWORD biSize; int32_t biWidth,biHeight; WORD biPlanes,biBitCount; DWORD biCompression;
};
struct BITMAPINFO {BITMAPINFOHEADER bmiHeader;};
struct Object {
  enum Kind {Font,Bitmap,Brush} kind;
  bool live=true,stock=false;
  std::wstring name;
  std::vector<uint32_t> pixels;
};
struct DC {bool live=true; Object *font=nullptr,*bitmap=nullptr; bool pending=false;};
using HGDIOBJ=Object*; using HFONT=Object*; using HBITMAP=Object*; using HBRUSH=Object*; using HDC=DC*;
namespace mock {
Object stockFont{Object::Font,true,true,L"stock",{}},stockBitmap{Object::Bitmap,true,true,L"",{}};
std::vector<std::unique_ptr<Object>> objects;
std::vector<std::unique_ptr<DC>> contexts;
int failedDeletes=0,draws=0,flushes=0;
bool onlyLastFont=false,noGlyphs=false,failDC=false,failBitmap=false,
    failFontSelect=false,failBitmapSelect=false,failMetrics=false,failDraw=false,failFlush=false;
std::wstring drawnFont;
Object *make(Object::Kind kind) {objects.push_back(std::make_unique<Object>());auto *o=objects.back().get();o->kind=kind;return o;}
DC *makeDC() {if(failDC)return nullptr;contexts.push_back(std::make_unique<DC>());auto *dc=contexts.back().get();dc->font=&stockFont;dc->bitmap=&stockBitmap;return dc;}
void reset() {
  contexts.clear();objects.clear();failedDeletes=draws=flushes=0;drawnFont.clear();
  onlyLastFont=noGlyphs=failDC=failBitmap=failFontSelect=failBitmapSelect=failMetrics=failDraw=failFlush=false;
}
int liveObjects() {int n=0;for(auto &o:objects)n+=o->live;for(auto &dc:contexts)n+=dc->live;return n;}
}
static HDC GetDC(void*) {return mock::makeDC();}
static HDC CreateCompatibleDC(HDC) {return mock::makeDC();}
static BOOL DeleteDC(HDC dc) {if(!dc)return FALSE;dc->live=false;return TRUE;}
static int ReleaseDC(void*,HDC dc) {return DeleteDC(dc);}
static HFONT CreateFontW(int,int,int,int,int,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,DWORD,const wchar_t *name) {
  auto *font=mock::make(Object::Font);font->name=name;return font;
}
static HGDIOBJ SelectObject(HDC dc,HGDIOBJ obj) {
  if(!obj || !dc || !dc->live)return nullptr;
  if(obj->kind==Object::Font) {if(mock::failFontSelect && !obj->stock)return nullptr;auto *old=dc->font;dc->font=obj;return old;}
  if(obj->kind==Object::Bitmap) {if(mock::failBitmapSelect && !obj->stock)return nullptr;auto *old=dc->bitmap;dc->bitmap=obj;return old;}
  return nullptr;
}
static BOOL DeleteObject(HGDIOBJ obj) {
  if(!obj)return FALSE;
  for(auto &dc:mock::contexts)if(dc->live && (dc->font==obj || dc->bitmap==obj)) {++mock::failedDeletes;return FALSE;}
  if(obj->stock || !obj->live) {++mock::failedDeletes;return FALSE;}
  obj->live=false;return TRUE;
}
static DWORD GetGlyphIndicesW(HDC dc,const wchar_t*,int,WORD *glyph,DWORD) {
  *glyph=(mock::noGlyphs || (mock::onlyLastFont && dc->font->name!=L"Segoe UI Symbol"))?0xffff:7;
  return 1;
}
static BOOL GetTextMetricsW(HDC,TEXTMETRICW *tm) {tm->tmHeight=8;return !mock::failMetrics;}
static BOOL GetTextExtentPoint32W(HDC,const wchar_t*,int,SIZE *extent) {extent->cx=6;extent->cy=8;return TRUE;}
static HBITMAP CreateDIBSection(HDC,const BITMAPINFO *info,unsigned,void **bits,void*,DWORD) {
  if(mock::failBitmap){*bits=nullptr;return nullptr;}
  auto *bmp=mock::make(Object::Bitmap);bmp->pixels.resize(info->bmiHeader.biWidth*std::abs(info->bmiHeader.biHeight));
  *bits=bmp->pixels.data();return bmp;
}
static HBRUSH CreateSolidBrush(DWORD) {return mock::make(Object::Brush);}
static int FillRect(HDC dc,const RECT*,HBRUSH) {std::fill(dc->bitmap->pixels.begin(),dc->bitmap->pixels.end(),0);return 1;}
static int SetBkMode(HDC,int) {return TRANSPARENT;}
static DWORD SetTextColor(HDC,DWORD) {return 0;}
static BOOL TextOutW(HDC dc,int,int,const wchar_t*,int) {
  ++mock::draws;mock::drawnFont=dc->font->name;
  if(mock::failDraw)return FALSE;
  dc->pending=true;return TRUE;
}
static BOOL GdiFlush() {
  ++mock::flushes;if(mock::failFlush)return FALSE;
  for(auto &dc:mock::contexts)if(dc->live && dc->pending) {
    if(!dc->bitmap->pixels.empty())dc->bitmap->pixels[dc->bitmap->pixels.size()/2]=0x00ffffff;
    dc->pending=false;
  }
  return TRUE;
}
struct GlyphMask {int width=1,height=1;std::vector<uint8_t> alpha;};
static float clampf(float x,float lo,float hi) {return std::max(lo,std::min(hi,x));}
#ifndef CHARWEAVE_WIN_GLYPH_SOURCE
#define CHARWEAVE_WIN_GLYPH_SOURCE "../src/WinGlyph.inc"
#endif
#include CHARWEAVE_WIN_GLYPH_SOURCE
static void require(bool v,const char *message) {if(!v)throw std::runtime_error(message);}
static GlyphMask render() {return MakeGlyphMask(u"中",u"",18);}
static void released() {require(mock::failedDeletes==0,"deleted an object while selected in a DC");require(mock::liveObjects()==0,"leaked GDI object/DC");}
int main() {
  int failures=0;
  auto test=[&](const char *name,const std::function<void()> &run) {
    mock::reset();try {run();std::cout<<"PASS "<<name<<'\n';}
    catch(const std::exception &e) {++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}
  };
  test("font and bitmap released",[]{render();released();});
  test("DIB reads wait for GDI",[]{auto mask=render();require(mock::flushes==1,"no GdiFlush before reading DIB bits");require(*std::max_element(mask.alpha.begin(),mask.alpha.end())==255,"queued drawing not visible in alpha mask");released();});
  test("missing glyph font fallback",[]{mock::onlyLastFont=true;render();require(mock::drawnFont==L"Segoe UI Symbol","accepted a font with no requested glyph");released();});
  test("unsupported glyph stays empty",[]{mock::noGlyphs=true;render();require(mock::draws==0,"rendered missing-glyph tofu");released();});
  test("DC creation failure",[]{mock::failDC=true;render();released();});
  test("DIB allocation failure",[]{mock::failBitmap=true;render();released();});
  test("font selection failure",[]{mock::failFontSelect=true;render();require(mock::draws==0,"drew with stock font after SelectObject failure");released();});
  test("bitmap selection failure",[]{mock::failBitmapSelect=true;render();require(mock::draws==0,"drew into the wrong bitmap");released();});
  test("font metrics failure",[]{mock::failMetrics=true;render();require(mock::draws==0,"ignored failed font metrics");released();});
  test("TextOut failure",[]{mock::failDraw=true;auto mask=render();require(std::all_of(mask.alpha.begin(),mask.alpha.end(),[](uint8_t a){return a==0;}),"read failed drawing");released();});
  test("flush failure",[]{mock::failFlush=true;auto mask=render();require(std::all_of(mask.alpha.begin(),mask.alpha.end(),[](uint8_t a){return a==0;}),"read unsynchronized pixels");released();});
  test("1000 repeated glyphs",[]{for(int i=0;i<1000;++i){render();released();}});
  mock::reset();
  if(failures) {std::cerr<<failures<<" GDI MODEL TESTS FAILED\n";return 1;}
  std::cout<<"ALL 12 GDI MODEL TESTS PASSED (not a Windows/AE host test)\n";
}
