/*
 * EdgeVizOutline.cpp — PF_Effect "EdgeViz" (com.edgeviz.outline) v3.1.2
 *
 * Cyclops-style layer visualization as a binary effect. Apply to any layer:
 *   structure group: edge frame + corner handles (3D layers projected through
 *     the active camera), merged mask silhouette (mode-aware rasterized
 *     outline, no dots/handles), shape layer drill-down (nested groups +
 *     parametric shapes), text glyph outlines, text frame, glyph vertices (all), shape key-vertex dots
 *     (curvature top-N), bezier handle lines, motion path
 *   pixel group: alpha/RGB luma edge outline (adjustment layers supported)
 *   Target=Layers Below (adjustment layers) visualizes every layer below.
 *   Language popup re-labels the panel (English/中文/한국어) via
 *   PF_Cmd_UPDATE_PARAMS_UI.
 *
 * Threading: all AEGP geometry is baked per frame in PF_Cmd_FRAME_SETUP and
 * cached in frame_data; in the GUI FRAME_SETUP arrives on a worker thread
 * where AEGP is skipped, and PF_Cmd_RENDER falls back to an inline bake
 * (all AEGP calls are index-enumerated or stream-API, error-checked, and
 * fail quietly — speculative GetNewStreamRefByMatchname probes are banned
 * because illegal matchname probes trip AE's internal verification).
 * The effect does NOT set PF_OutFlag2_SUPPORTS_THREADED_RENDERING.
 */

// Carbon-era type shims: Fixed/Boolean/Handle were removed from modern
// macOS SDKs but the CS6 AE headers still reference them.
#ifndef EDGEVIZ_CARBON_SHIMS
#define EDGEVIZ_CARBON_SHIMS
#include <stdint.h>
typedef int32_t       Fixed;
typedef unsigned char Boolean;
typedef void        **Handle;
#endif

#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "Param_Utils.h"
#include "AE_GeneralPlug.h"
#include <stdio.h>
#include <string.h>

struct Suites {
  AEGP_PFInterfaceSuite1  *pfi;
  AEGP_LayerSuite7        *lay;
  AEGP_StreamSuite4       *str;
  AEGP_DynamicStreamSuite4 *dyn;
  AEGP_MaskOutlineSuite3  *mos;
  AEGP_TextLayerSuite1    *txt;
  AEGP_MaskSuite6         *mask;
  AEGP_ItemSuite8         *item;
  AEGP_CompSuite9         *comp;
  AEGP_MemorySuite1       *mem;
};

static PF_Err AcquireSuites(PF_InData *in_data, Suites *s)
{
  SPBasicSuite *sb = in_data->pica_basicP;
  memset(s, 0, sizeof(*s));
  // best-effort per suite (AE 26 suite availability varies)
  sb->AcquireSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1, (const void **)&s->pfi);
  sb->AcquireSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion7, (const void **)&s->lay);
  sb->AcquireSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion4, (const void **)&s->str);
  sb->AcquireSuite(kAEGPDynamicStreamSuite, kAEGPDynamicStreamSuiteVersion4, (const void **)&s->dyn);
  sb->AcquireSuite(kAEGPMaskOutlineSuite, kAEGPMaskOutlineSuiteVersion3, (const void **)&s->mos);
  sb->AcquireSuite(kAEGPTextLayerSuite, kAEGPTextLayerSuiteVersion1, (const void **)&s->txt);
  sb->AcquireSuite(kAEGPMaskSuite, kAEGPMaskSuiteVersion6, (const void **)&s->mask);
  sb->AcquireSuite(kAEGPItemSuite, kAEGPItemSuiteVersion8, (const void **)&s->item);
  sb->AcquireSuite(kAEGPCompSuite, kAEGPCompSuiteVersion9, (const void **)&s->comp);
  sb->AcquireSuite(kAEGPMemorySuite, kAEGPMemorySuiteVersion1, (const void **)&s->mem);
  return PF_Err_NONE;
}

static void ReleaseSuites(PF_InData *in_data, Suites *s)
{
  SPBasicSuite *sb = in_data->pica_basicP;
  if (s->mem) sb->ReleaseSuite(kAEGPMemorySuite, kAEGPMemorySuiteVersion1);
  if (s->comp) sb->ReleaseSuite(kAEGPCompSuite, kAEGPCompSuiteVersion9);
  if (s->item) sb->ReleaseSuite(kAEGPItemSuite, kAEGPItemSuiteVersion8);
  if (s->mask) sb->ReleaseSuite(kAEGPMaskSuite, kAEGPMaskSuiteVersion6);
  if (s->txt) sb->ReleaseSuite(kAEGPTextLayerSuite, kAEGPTextLayerSuiteVersion1);
  if (s->mos) sb->ReleaseSuite(kAEGPMaskOutlineSuite, kAEGPMaskOutlineSuiteVersion3);
  if (s->dyn) sb->ReleaseSuite(kAEGPDynamicStreamSuite, kAEGPDynamicStreamSuiteVersion4);
  if (s->str) sb->ReleaseSuite(kAEGPStreamSuite, kAEGPStreamSuiteVersion4);
  if (s->lay) sb->ReleaseSuite(kAEGPLayerSuite, kAEGPLayerSuiteVersion7);
  if (s->pfi) sb->ReleaseSuite(kAEGPPFInterfaceSuite, kAEGPPFInterfaceSuiteVersion1);
}

#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>

#define PLUGIN_NAME        "EdgeViz"
#define MAJOR_VERSION      3
#define MINOR_VERSION      1
#define BUG_VERSION        2
#define STAGE_VERSION      PF_Stage_RELEASE
#define BUILD_VERSION      1

/* Runtime indices follow UI order; uu.id retains the original on-disk ID.
   Reordered controls and newly appended group IDs were rendered against old
   projects with non-default settings before release. Never reuse a DISK_* ID. */
enum {
  PARAM_INPUT = 0,
  PARAM_TARGET,
  PARAM_TOPIC_LANG,
  PARAM_LANGUAGE,
  PARAM_LANG_END,
  PARAM_TOPIC_STRUCT,
  PARAM_SHOW_FRAME,
  PARAM_SHOW_TFRAME,
  PARAM_SHOW_TEXT,
  PARAM_SHOW_SHAPE,
  PARAM_SHOW_MASK,
  PARAM_SHOW_MOTION,
  PARAM_SHOW_VERTS,
  PARAM_SHOW_HANDLES,
  PARAM_THIN_N,
  PARAM_STRUCT_END,
  PARAM_TOPIC_STYLE,
  PARAM_STROKE_W,
  PARAM_COL_FRAME,
  PARAM_COL_PATH,
  PARAM_COL_HANDLE,
  PARAM_COL_MOTION,
  PARAM_STYLE_END,
  PARAM_TOPIC_MORE,
  PARAM_VERT_SIZE,
  PARAM_HANDLE_SIZE,
  PARAM_HANDLE_LENGTH,
  PARAM_POINT_STYLE,
  PARAM_CUSTOM_POINT_LAYER,
  PARAM_MORE_END,
  PARAM_PX_ON,
  PARAM_TOPIC_PIXEL,
  PARAM_COLOR,
  PARAM_WIDTH,
  PARAM_THRESHOLD,
  PARAM_MODE,
  PARAM_INVERT,
  PARAM_ONLY,
  PARAM_FADE,
  PARAM_PIXEL_END,
  PARAM_COUNT
};

enum {
  DISK_COLOR = 1, DISK_WIDTH = 2, DISK_THRESHOLD = 3,
  DISK_INVERT = 4, DISK_ONLY = 5, DISK_FADE = 6, DISK_MODE = 7,
  DISK_TOPIC_STRUCT = 8, DISK_SHOW_FRAME = 9, DISK_SHOW_MASK = 10,
  DISK_SHOW_VERTS = 11, DISK_SHOW_HANDLES = 12, DISK_SHOW_TEXT = 13,
  DISK_SHOW_SHAPE = 14, DISK_SHOW_MOTION = 15, DISK_THIN_N = 16,
  DISK_PX_ON = 17, DISK_STRUCT_END = 18, DISK_TOPIC_STYLE = 19,
  DISK_STROKE_W = 20, DISK_COL_FRAME = 21, DISK_COL_PATH = 22,
  DISK_COL_HANDLE = 23, DISK_COL_MOTION = 24, DISK_STYLE_END = 25,
  DISK_TARGET = 26, DISK_TOPIC_MORE = 27, DISK_SHOW_TFRAME = 28,
  DISK_VERT_SIZE = 29, DISK_HANDLE_SIZE = 30, DISK_MORE_END = 31,
  DISK_TOPIC_LANG = 32, DISK_LANGUAGE = 33, DISK_LANG_END = 34,
  DISK_TOPIC_PIXEL = 35, DISK_PIXEL_END = 36,
  DISK_HANDLE_LENGTH = 37, DISK_POINT_STYLE = 38, DISK_CUSTOM_POINT_LAYER = 39
};

#define GEO_MAGIC 0x45564731 /* 'EVG1' */
#define MOTION_SAMPLES 120
#define TRACK_SAMPLES  240
#define FLAT_PER_SPAN  12
#define GEO_MAX_FRAMES 32

struct Pt      { float x, y; };
struct XSample { float px, py, ax, ay, sx, sy, rot; };
struct PathDesc {
  A_long off, n, closed, kind; // kind 0=structure, 1=motion
  A_long occ;                    // occluder index for motion paths, -1 otherwise
};
struct KeyVert { float x, y, tinX, tinY, toutX, toutY; A_long flags; };
struct OccQuad { Pt p[4]; };     // convex comp/layer-space occluder

/* render-time geometry blob layout (single flat allocation):
   [GeoHeader][XSample track[TRACK_SAMPLES]][Pt motion[MOTION_SAMPLES+1]]
   [PathDesc paths[GEO_MAX_PATHS] reserved][KeyVert keys[GEO_MAX_KEYS] reserved]
   [Pt frames[GEO_MAX_FRAMES*4] reserved][OccQuad occ[GEO_MAX_OCC] reserved]
   [Pt flat[flatTotal]]
   pathCount/keyCount/frameN/occCount/flatTotal count the used entries */
struct GeoHeader {
  A_long magic;
  A_long totalBytes;
  double t0, t1;      // track table time range in seconds
  A_long trackN;
  A_long motionN;
  A_long pathCount;
  A_long flatTotal;
  A_long keyCount;
  A_long frameN;      // used entries of the frame-quad reserve (4 Pt each, comp space)
  A_long occCount;    // used entries of the occluder reserve
  A_long frameBase;   // byte offset of the frame-quad array within the blob
  A_long occBase;     // byte offset of the OccQuad array within the blob
  A_long keyBase;     // byte offset of the KeyVert array within the blob
  A_long flatBase;    // byte offset of the flat Pt array within the blob
  A_long status;      // bit0: motion skipped(3D/parent), bit1: shape walk failed,
                      // bit2: comp-space buffer (vector layer), bit3: baked in "layers below" mode
};

static PF_Err
About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  PF_SPRINTF(out_data->return_msg, "%s v%d.%d.%d — layer visualization", PLUGIN_NAME, MAJOR_VERSION, MINOR_VERSION, BUG_VERSION);
  return PF_Err_NONE;
}

static PF_Err
ParamsSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;

  // High-frequency scope and language controls stay at the top.
  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_ECW_SEPARATOR;
  PF_ADD_POPUP("Target", 3, 1, "Auto|This Layer|Layers Below|", DISK_TARGET);

  AEFX_CLR_STRUCT(def);
  def.flags = PF_ParamFlag_START_COLLAPSED;
  PF_ADD_TOPIC("01 Language", DISK_TOPIC_LANG);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Language", 3, 1,
               "English|\xE4\xB8\xAD\xE6\x96\x87|\xED\x95\x9C\xEA\xB5\xAD\xEC\x96\xB4|",
               DISK_LANGUAGE);
  AEFX_CLR_STRUCT(def);
  PF_END_TOPIC(DISK_LANG_END);

  AEFX_CLR_STRUCT(def);
  PF_ADD_TOPIC("02 Structure", DISK_TOPIC_STRUCT);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Frame", "", 1, 0, DISK_SHOW_FRAME);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Text Frame", "", 1, 0, DISK_SHOW_TFRAME);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Text Glyphs", "", 1, 0, DISK_SHOW_TEXT);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Shape Drill-down", "", 1, 0, DISK_SHOW_SHAPE);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Mask Silhouette", "", 1, 0, DISK_SHOW_MASK);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Motion Path", "", 1, 0, DISK_SHOW_MOTION);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Vertex Dots", "", 1, 0, DISK_SHOW_VERTS);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Bezier Handles", "", 1, 0, DISK_SHOW_HANDLES);
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("Key Vertices", 4, 24, 4, 24, 12, DISK_THIN_N);
  AEFX_CLR_STRUCT(def); PF_END_TOPIC(DISK_STRUCT_END);

  AEFX_CLR_STRUCT(def);
  def.flags = PF_ParamFlag_START_COLLAPSED;
  PF_ADD_TOPIC("03 Style", DISK_TOPIC_STYLE);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Stroke Width", 0.5, 16, 0.5, 16, 2.0, 1, 0, 0, DISK_STROKE_W);
  AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Frame Color", 255, 204, 0, DISK_COL_FRAME);
  AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Path Color", 26, 217, 255, DISK_COL_PATH);
  AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Handle Color", 255, 115, 26, DISK_COL_HANDLE);
  AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Motion Color", 255, 51, 191, DISK_COL_MOTION);
  AEFX_CLR_STRUCT(def); PF_END_TOPIC(DISK_STYLE_END);

  AEFX_CLR_STRUCT(def);
  def.flags = PF_ParamFlag_START_COLLAPSED;
  PF_ADD_TOPIC("04 Detail / Point Style", DISK_TOPIC_MORE);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Vertex Size", 2, 16, 2, 16, 5.0, 1, 0, 0, DISK_VERT_SIZE);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Handle Size", 2, 16, 2, 16, 4.0, 1, 0, 0, DISK_HANDLE_SIZE);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Handle Length", 0, 300, 0, 300, 100.0, 1, 0, 0, DISK_HANDLE_LENGTH);
  AEFX_CLR_STRUCT(def); PF_ADD_POPUP("Point Style", 6, 1,
                                        "Circle|Square|Triangle|Diamond|Cross|Custom Layer|",
                                        DISK_POINT_STYLE);
  AEFX_CLR_STRUCT(def); PF_ADD_LAYER("Custom Point Layer", PF_LayerDefault_NONE, DISK_CUSTOM_POINT_LAYER);
  AEFX_CLR_STRUCT(def); PF_END_TOPIC(DISK_MORE_END);

  // Pixel outline enable is intentionally a primary switch; detailed pixel
  // controls stay in the collapsed group to reduce UI scanning cost.
  AEFX_CLR_STRUCT(def);
  def.ui_flags = PF_PUI_ECW_SEPARATOR;
  PF_ADD_CHECKBOX("Enable Pixel Outline", "", 0, 0, DISK_PX_ON);
  AEFX_CLR_STRUCT(def);
  def.flags = PF_ParamFlag_START_COLLAPSED;
  PF_ADD_TOPIC("05 Pixel Outline", DISK_TOPIC_PIXEL);
  AEFX_CLR_STRUCT(def); PF_ADD_COLOR("Color", 255, 204, 0, DISK_COLOR);
  AEFX_CLR_STRUCT(def); PF_ADD_FLOAT_SLIDERX("Width", 1, 16, 1, 16, 2.0, 1, 0, 0, DISK_WIDTH);
  AEFX_CLR_STRUCT(def); PF_ADD_SLIDER("Edge Threshold", 0, 255, 0, 255, 48, DISK_THRESHOLD);
  AEFX_CLR_STRUCT(def); PF_ADD_POPUP("Edge Source", 3, 3, "Alpha|RGB Luma|Alpha+RGB|", DISK_MODE);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Invert", "", 0, 0, DISK_INVERT);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Outline Only", "", 0, 0, DISK_ONLY);
  AEFX_CLR_STRUCT(def); PF_ADD_CHECKBOX("Distance Fade", "", 0, 0, DISK_FADE);
  AEFX_CLR_STRUCT(def); PF_END_TOPIC(DISK_PIXEL_END);

  out_data->num_params = PARAM_COUNT;
  return err;
}

static const char *kParamNames[PARAM_COUNT][3] = {
  {0, 0, 0}, // input
  {"Target", "目标范围", "대상 범위"},
  {"01 Language", "01 语言", "01 언어"},
  {"Language", "语言", "언어"},
  {0, 0, 0},
  {"02 Structure", "02 结构可视化", "02 구조 시각화"},
  {"Frame", "外框", "프레임"},
  {"Text Frame", "文字外框", "텍스트 프레임"},
  {"Text Glyphs", "文字字形", "텍스트 글리프"},
  {"Shape Drill-down", "形状下钻", "셰이프 드릴다운"},
  {"Mask Silhouette", "遮罩剪影", "마스크 실루엣"},
  {"Motion Path", "运动路径", "모션 패스"},
  {"Vertex Dots", "顶点圆点", "버텍스 점"},
  {"Bezier Handles", "贝塞尔手柄", "베지어 핸들"},
  {"Key Vertices", "关键顶点", "키 버텍스"},
  {0, 0, 0},
  {"03 Style", "03 样式", "03 스타일"},
  {"Stroke Width", "线宽", "선 너비"},
  {"Frame Color", "外框颜色", "프레임 색상"},
  {"Path Color", "路径颜色", "패스 색상"},
  {"Handle Color", "手柄颜色", "핸들 색상"},
  {"Motion Color", "运动路径颜色", "모션 색상"},
  {0, 0, 0},
  {"04 Detail / Point Style", "04 细节 / 点样式", "04 디테일 / 점 스타일"},
  {"Vertex Size", "顶点尺寸", "버텍스 크기"},
  {"Handle Size", "手柄尺寸", "핸들 크기"},
  {"Handle Length", "手柄长度", "핸들 길이"},
  {"Point Style", "路径点样式", "점 스타일"},
  {"Custom Point Layer", "自定义点图层", "사용자 지정 점 레이어"},
  {0, 0, 0},
  {"Enable Pixel Outline", "启用像素轮廓", "픽셀 아웃라인 켜기"},
  {"05 Pixel Outline", "05 像素轮廓", "05 픽셀 아웃라인"},
  {"Color", "颜色", "색상"},
  {"Width", "宽度", "너비"},
  {"Edge Threshold", "边缘阈值", "가장자리 임계값"},
  {"Edge Source", "边缘来源", "가장자리 소스"},
  {"Invert", "反转", "반전"},
  {"Outline Only", "仅轮廓", "아웃라인만"},
  {"Distance Fade", "距离淡出", "거리 페이드"},
  {0, 0, 0}
};

/* PF_Cmd_UPDATE_PARAMS_UI: re-label every parameter for the selected language.
   Names only (matchname untouched); popup option lists are not renamable via
   PF_UpdateParamUI and stay English. */
static PF_Err
UpdateParamsUI(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  A_long lang = params[PARAM_LANGUAGE]->u.pd.value;
  if (lang < 1 || lang > 3) lang = 1;
  PF_ParamUtilsSuite3 *pu = NULL;
  if (in_data->pica_basicP->AcquireSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3,
                                         (const void **)&pu) || !pu)
    return PF_Err_NONE;
  for (int i = 1; i < PARAM_COUNT; i++) {
    const char *nm = kParamNames[i][lang - 1];
    if (!nm || !nm[0]) continue;
    if (!strcmp(params[i]->name, nm)) continue;
    PF_ParamDef def = *params[i];
    strncpy(def.name, nm, PF_MAX_EFFECT_PARAM_NAME_LEN);
    def.name[PF_MAX_EFFECT_PARAM_NAME_LEN] = 0;
    pu->PF_UpdateParamUI(in_data->effect_ref, i, &def);
  }
  in_data->pica_basicP->ReleaseSuite(kPFParamUtilsSuite, kPFParamUtilsSuiteVersion3);
  return PF_Err_NONE;
}

/* ================= geometry bake (main thread only: GLOBAL_SETUP) ================= */

struct Blob {
  char  *base;
  A_long cap;
  A_long len;
  PF_Err err;
};
static void *BlobAt(Blob *b, A_long need)
{
  if (b->err) return NULL;
  if (b->len + need > b->cap) { b->err = PF_Err_OUT_OF_MEMORY; return NULL; }
  void *p = b->base + b->len;
  b->len += need;
  return p;
}
static inline double NowSec(PF_InData *in_data, A_long v)
{
  return (double)v / (double)in_data->time_scale;
}
static inline void TimeOf(PF_InData *in_data, double sec, A_Time *t)
{
  t->value = (A_long)(sec * (double)in_data->time_scale + 0.5);
  t->scale = in_data->time_scale;
}

/* group transform compose, same order as the ExtendScript version:
   M = T(pos) * R(rot) * K(skew) * S(scale) * T(-anchor) */
struct Mat { double a, b, c, d, e, f; };
static Mat MatMul(const Mat &m1, const Mat &m2)
{
  Mat r;
  r.a = m1.a * m2.a + m1.c * m2.b;
  r.b = m1.b * m2.a + m1.d * m2.b;
  r.c = m1.a * m2.c + m1.c * m2.d;
  r.d = m1.b * m2.c + m1.d * m2.d;
  r.e = m1.a * m2.e + m1.c * m2.f + m1.e;
  r.f = m1.b * m2.e + m1.d * m2.f + m1.f;
  return r;
}
static Mat MatId() { Mat m = {1, 0, 0, 1, 0, 0}; return m; }
static Mat MatTrans(double x, double y) { Mat m = {1, 0, 0, 1, x, y}; return m; }
static Mat MatRot(double deg) {
  double r = deg * M_PI / 180.0, c = cos(r), s = sin(r);
  Mat m = {c, s, -s, c, 0, 0}; return m;
}
static Mat MatScale(double sx, double sy) { Mat m = {sx, 0, 0, sy, 0, 0}; return m; }
static Mat MatSkew(double skewDeg, double axisDeg) {
  double sh = tan(skewDeg * M_PI / 180.0), sa = axisDeg * M_PI / 180.0;
  Mat m = {1, sh * sin(sa), sh * cos(sa), 1, 0, 0}; return m;
}
static inline Pt MatApply(const Mat &m, double x, double y)
{
  Pt p = {(float)(m.a * x + m.c * y + m.e), (float)(m.b * x + m.d * y + m.f)};
  return p;
}

/* cubic bezier flatten, fixed subdivisions per span */
static void FlattenSpan(Blob *b, float x0, float y0, float c1x, float c1y,
                        float c2x, float c2y, float x1, float y1)
{
  for (int k = 0; k < FLAT_PER_SPAN; k++) {
    double t = (double)k / FLAT_PER_SPAN, u = 1.0 - t;
    double x = u*u*u*x0 + 3*u*u*t*c1x + 3*u*t*t*c2x + t*t*t*x1;
    double y = u*u*u*y0 + 3*u*u*t*c1y + 3*u*t*t*c2y + t*t*t*y1;
    Pt *p = (Pt *)BlobAt(b, sizeof(Pt));
    if (p) { p->x = (float)x; p->y = (float)y; }
  }
}

/* curvature-based key vertex selection: first + top N by turning angle */
static void SelectKeys(const Pt *v, A_long n, bool closed, A_long maxKeep, bool *keep)
{
  for (A_long i = 0; i < n; i++) keep[i] = false;
  if (n <= 0) return;
  if (n <= maxKeep) { for (A_long i = 0; i < n; i++) keep[i] = true; return; }
  keep[0] = true;
  A_long kept = 1;
  // simple 2-pass greedy: repeatedly take the sharpest remaining vertex
  float *score = (float *)malloc(sizeof(float) * n);
  for (A_long i = 0; i < n; i++) {
    A_long ip = closed ? (i - 1 + n) % n : (i > 0 ? i - 1 : 0);
    A_long in = closed ? (i + 1) % n : (i < n - 1 ? i + 1 : n - 1);
    double d1x = v[i].x - v[ip].x, d1y = v[i].y - v[ip].y;
    double d2x = v[in].x - v[i].x, d2y = v[in].y - v[i].y;
    double l1 = sqrt(d1x * d1x + d1y * d1y), l2 = sqrt(d2x * d2x + d2y * d2y);
    double dot = (l1 > 0.001 && l2 > 0.001) ? (d1x * d2x + d1y * d2y) / (l1 * l2) : 1.0;
    if (dot > 1) dot = 1; if (dot < -1) dot = -1;
    score[i] = (float)acos(dot);
  }
  while (kept < maxKeep) {
    A_long best = -1; float bs = -1;
    for (A_long i = 0; i < n; i++) {
      if (!keep[i] && score[i] > bs) { bs = score[i]; best = i; }
    }
    if (best < 0) break;
    keep[best] = true; kept++;
  }
  free(score);
}

#define GEO_MAX_PATHS 512
#define GEO_MAX_KEYS 8192
#define GEO_MAX_OCC 128

struct BakeCtx {
  PF_InData *in_data;
  Blob *blob;
  A_long pdBase;   // byte offset of the consecutive PathDesc array
  A_long keyBase;  // byte offset of the consecutive KeyVert array
  A_long frameBase; // byte offset of the frame-quad reserve (GEO_MAX_FRAMES*4 Pt)
  A_long occBase;   // byte offset of the occluder reserve
  A_long flatStart;
  A_long thinN;
  bool  showTFrame; // Text Frame toggle (frames for text layers)
  bool  showHandles; // preserve all vector vertices when handles are enabled
  bool  needKeys;    // bake key vertices only when dots or handles are rendered
  bool  needMotion;  // bake motion samples only when motion/frame needs them
};

/* append one flattened+keyed path (bezier data in arbitrary space, pre-transformed).
   Text outlines opt into all vertices: Key Vertices is a thinning control for
   masks/shapes, not a reason to hide glyph vertices or handles in mixed text. */
static void AppendPathDataEx(BakeCtx *cx, const PF_PathVertex *verts, A_long nSegs,
                             bool closed, bool keepAllKeys)
{
  Blob *b = cx->blob;
  A_long nV = closed ? nSegs : nSegs + 1;
  if (nV < 2) return;
  GeoHeader *gh = (GeoHeader *)b->base;
  if (gh->pathCount >= GEO_MAX_PATHS) return;
  PathDesc *pd = (PathDesc *)(b->base + cx->pdBase + gh->pathCount * sizeof(PathDesc));
  gh->pathCount++;
  pd->off = (A_long)((b->len - cx->flatStart) / sizeof(Pt)); // Pt units, flat points only
  pd->closed = closed ? 1 : 0;
  pd->kind = 0; // structure path (colPath); motion polylines set kind=1 themselves
  pd->occ = -1;
  for (A_long i = 0; i < nSegs; i++) {
    const PF_PathVertex *v0 = &verts[i];
    const PF_PathVertex *v1 = &verts[i + 1];
    FlattenSpan(b, (float)v0->x, (float)v0->y,
                  (float)(v0->x + v0->tan_out_x), (float)(v0->y + v0->tan_out_y),
                  (float)(v1->x + v1->tan_in_x), (float)(v1->y + v1->tan_in_y),
                  (float)v1->x, (float)v1->y);
    if (b->err) break;
  }
  pd->n = (A_long)((b->len - cx->flatStart) / sizeof(Pt)) - pd->off;
  if (b->err || !cx->needKeys) return;

  // key vertices: text keeps every vertex so every glyph has complete points
  // and handles, while masks/shapes retain the configurable curvature thinning.
  Pt *pv = (Pt *)malloc(sizeof(Pt) * nV);
  if (!pv) return;
  for (A_long i = 0; i < nV; i++) { pv[i].x = (float)verts[i].x; pv[i].y = (float)verts[i].y; }
  bool *keep = (bool *)malloc(sizeof(bool) * nV);
  if (keep) {
    if (keepAllKeys) {
      for (A_long i = 0; i < nV; i++) keep[i] = true;
    } else {
      SelectKeys(pv, nV, closed, cx->thinN, keep);
    }
    for (A_long i = 0; i < nV; i++) {
      if (!keep[i]) continue;
      if (gh->keyCount >= GEO_MAX_KEYS) break;
      KeyVert *kv = (KeyVert *)(b->base + cx->keyBase + gh->keyCount * sizeof(KeyVert));
      if (!kv) break;
      kv->x = pv[i].x; kv->y = pv[i].y;
      float inX = (float)verts[i].tan_in_x, inY = (float)verts[i].tan_in_y;
      float outX = (float)verts[i].tan_out_x, outY = (float)verts[i].tan_out_y;
      A_long prev = closed ? ((i + nV - 1) % nV) : (i > 0 ? i - 1 : (i + 1 < nV ? i + 1 : i));
      A_long next = closed ? ((i + 1) % nV) : (i + 1 < nV ? i + 1 : (i > 0 ? i - 1 : i));
      bool synthetic = false;
      // AE returns zero tangents for straight glyph corners and many parametric
      // shape corners. For visualization, expose a short tangent marker derived
      // from adjacent vertices so every selected point has a visible handle.
      if (fabsf(inX) + fabsf(inY) <= 0.01f && prev != i) {
        float dx = pv[prev].x - pv[i].x, dy = pv[prev].y - pv[i].y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.01f) { inX = dx * 0.24f; inY = dy * 0.24f; synthetic = true; }
      }
      if (fabsf(outX) + fabsf(outY) <= 0.01f && next != i) {
        float dx = pv[next].x - pv[i].x, dy = pv[next].y - pv[i].y;
        float len = sqrtf(dx * dx + dy * dy);
        if (len > 0.01f) { outX = dx * 0.24f; outY = dy * 0.24f; synthetic = true; }
      }
      kv->tinX = inX; kv->tinY = inY;
      kv->toutX = outX; kv->toutY = outY;
      kv->flags = (keepAllKeys ? 1 : 0) | (synthetic ? 2 : 0);
      gh->keyCount++;
    }
    free(keep);
  }
  free(pv);
}

static void AppendPathData(BakeCtx *cx, const PF_PathVertex *verts, A_long nSegs, bool closed)
{
  AppendPathDataEx(cx, verts, nSegs, closed, cx->showHandles);
}

/* read a mask-outline handle's vertices into PF_PathVertex array (malloc'd) */
static PF_PathVertex *ReadOutlineVerts(Suites *suites, AEGP_MaskOutlineValH mo,
                                       A_long *nSegsP, bool *closedP)
{
  *nSegsP = 0; *closedP = false;
  if (!mo) return NULL;
  A_Boolean openB = TRUE;
  A_long nSegs = 0;
  if (suites->mos->AEGP_IsMaskOutlineOpen(mo, &openB)) return NULL;
  if (suites->mos->AEGP_GetMaskOutlineNumSegments(mo, &nSegs)) return NULL;
  if (nSegs < 1) return NULL;
  A_long nV = openB ? nSegs + 1 : nSegs;
  PF_PathVertex *v = (PF_PathVertex *)malloc(sizeof(PF_PathVertex) * (nV + 1));
  if (!v) return NULL;
  for (A_long i = 0; i < nV; i++) {
    if (suites->mos->AEGP_GetMaskOutlineVertexInfo(mo, i, (AEGP_MaskVertex *)&v[i])) {
      // Never continue with a partially initialized outline. A single bad
      // vertex used to leak undefined coordinates into masks/precomps.
      free(v);
      return NULL;
    }
  }
  if (!openB) v[nV] = v[0]; // close the loop for the flattener
  *nSegsP = nSegs;
  *closedP = !openB;
  return v;
}

/* ---- bake helpers that need AEGP (main thread only) ---- */

/* layer transform (position/anchor/scale/rotation) at one time, spatial-aware */
struct LayerX { double px, py, ax, ay, sx, sy, rot; };
static void ReadLayerXform(Suites *suites, AEGP_LayerH layerH, A_Time *t, LayerX *o)
{
  o->px = o->py = o->ax = o->ay = o->rot = 0;
  o->sx = o->sy = 100;
  AEGP_StreamVal2 v;
  AEGP_StreamType ty = AEGP_StreamType_NO_DATA;
  if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_POSITION,
                                             AEGP_LTimeMode_CompTime, t, FALSE, &v, &ty)) {
    if (ty == AEGP_StreamType_ThreeD || ty == AEGP_StreamType_ThreeD_SPATIAL) {
      o->px = v.three_d.x; o->py = v.three_d.y;
    } else { o->px = v.two_d.x; o->py = v.two_d.y; }
  }
  if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_ANCHORPOINT,
                                             AEGP_LTimeMode_CompTime, t, FALSE, &v, &ty)) {
    if (ty == AEGP_StreamType_ThreeD || ty == AEGP_StreamType_ThreeD_SPATIAL) {
      o->ax = v.three_d.x; o->ay = v.three_d.y;
    } else { o->ax = v.two_d.x; o->ay = v.two_d.y; }
  }
  if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_SCALE,
                                             AEGP_LTimeMode_CompTime, t, FALSE, &v, &ty)) {
    if (ty == AEGP_StreamType_ThreeD || ty == AEGP_StreamType_ThreeD_SPATIAL) {
      o->sx = v.three_d.x; o->sy = v.three_d.y;
    } else { o->sx = v.two_d.x; o->sy = v.two_d.y; }
  }
  if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_ROTATION,
                                             AEGP_LTimeMode_CompTime, t, FALSE, &v, NULL)) {
    o->rot = v.one_d;
  }
}

static Mat MatFromLayerX(const LayerX *o)
{
  Mat m = MatTrans(o->px, o->py);
  m = MatMul(m, MatRot(o->rot));
  m = MatMul(m, MatScale(o->sx / 100.0, o->sy / 100.0));
  m = MatMul(m, MatTrans(-o->ax, -o->ay));
  return m;
}

static A_Time LayerTimeAtComp(Suites *suites, AEGP_LayerH layerH, const A_Time *compT)
{
  A_Time layerT = *compT;
  if (suites->lay) {
    A_Time converted;
    if (!suites->lay->AEGP_ConvertCompToLayerTime(layerH, compT, &converted))
      layerT = converted;
  }
  return layerT;
}

static A_long MotionPrefixCount(PF_InData *in_data, double t0, double t1, A_long n)
{
  if (n < 2) return n;
  double dur = t1 - t0;
  if (dur <= 1e-9) return n;
  double f = (NowSec(in_data, in_data->current_time) - t0) / dur;
  if (f < 0) f = 0; if (f > 1) f = 1;
  A_long k = 2 + (A_long)(f * (double)(n - 1));
  if (k > n) k = n; if (k < 2) k = 2;
  return k;
}

static PF_Err BakeMotionAndTrack(Suites *suites, PF_InData *in_data,
                                 AEGP_LayerH layerH, Blob *blob)
{
  PF_Err err = PF_Err_NONE;
  GeoHeader *gh = (GeoHeader *)blob->base;
  double dur = NowSec(in_data, in_data->total_time);
  if (!suites->lay || !suites->str) { gh->status |= 2; return err; }

  // transform track table (position/anchor/scale/rotation over time)
  XSample *tr = (XSample *)BlobAt(blob, sizeof(XSample) * TRACK_SAMPLES);
  if (tr) {
    gh->t0 = 0; gh->t1 = dur;
    gh->trackN = TRACK_SAMPLES;
    for (A_long i = 0; i < TRACK_SAMPLES; i++) {
      double sec = dur * (double)i / (TRACK_SAMPLES - 1);
      A_Time t; TimeOf(in_data, sec, &t);
      AEGP_StreamVal2 pv, av, sv, rv;
      tr[i].px = tr[i].py = 0;
      tr[i].ax = tr[i].ay = 0;
      tr[i].sx = tr[i].sy = 100;
      tr[i].rot = 0;
      if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_POSITION,
                                                            AEGP_LTimeMode_CompTime, &t, FALSE, &pv, NULL)) {
        tr[i].px = (float)pv.two_d.x; tr[i].py = (float)pv.two_d.y;
      }
      AEGP_StreamType aty = AEGP_StreamType_NO_DATA, sty = aty, rty = aty;
      if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_ANCHORPOINT,
                                                            AEGP_LTimeMode_CompTime, &t, FALSE, &av, &aty)) {
        if (aty == AEGP_StreamType_ThreeD || aty == AEGP_StreamType_ThreeD_SPATIAL) {
          tr[i].ax = (float)av.three_d.x; tr[i].ay = (float)av.three_d.y;
        } else {
          tr[i].ax = (float)av.two_d.x; tr[i].ay = (float)av.two_d.y;
        }
      }
      if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_SCALE,
                                                            AEGP_LTimeMode_CompTime, &t, FALSE, &sv, &sty)) {
        if (sty == AEGP_StreamType_ThreeD || sty == AEGP_StreamType_ThreeD_SPATIAL) {
          tr[i].sx = (float)sv.three_d.x; tr[i].sy = (float)sv.three_d.y;
        } else {
          tr[i].sx = (float)sv.two_d.x; tr[i].sy = (float)sv.two_d.y;
        }
      }
      if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_ROTATION,
                                                            AEGP_LTimeMode_CompTime, &t, FALSE, &rv, &rty)) {
        tr[i].rot = (float)rv.one_d;
      }
    }
  }
  // parented layers: skip (motion path mapping needs comp space)
  AEGP_LayerH parentH = NULL;
  if (!suites->lay->AEGP_GetLayerParent(layerH, &parentH) && parentH) {
    gh->status |= 1;
  }

  // motion polyline from the position stream (2D root layers only)
  AEGP_StreamType posType = AEGP_StreamType_NO_DATA;
  A_Time t0; TimeOf(in_data, 0, &t0);
  AEGP_StreamVal2 v0val;
  A_Err posErr = suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_POSITION,
                                                        AEGP_LTimeMode_CompTime, &t0, FALSE,
                                                        &v0val, &posType);
  if (!posErr) {
    bool is2D = (posType == AEGP_StreamType_TwoD || posType == AEGP_StreamType_TwoD_SPATIAL ||
                 posType == AEGP_StreamType_ThreeD || posType == AEGP_StreamType_ThreeD_SPATIAL);
    if (is2D && !(gh->status & 1)) {
      Pt *mp = (Pt *)BlobAt(blob, sizeof(Pt) * (MOTION_SAMPLES + 1));
      if (mp) {
        A_long n = 0;
        for (A_long i = 0; i <= MOTION_SAMPLES; i++) {
          double sec = dur * (double)i / MOTION_SAMPLES;
          A_Time t; TimeOf(in_data, sec, &t);
          AEGP_StreamVal2 val;
          if (!suites->str->AEGP_GetLayerStreamValue(layerH, AEGP_LayerStream_POSITION,
                                                                AEGP_LTimeMode_CompTime, &t, FALSE,
                                                                &val, NULL)) {
            if (posType == AEGP_StreamType_ThreeD || posType == AEGP_StreamType_ThreeD_SPATIAL) {
              mp[n].x = (float)val.three_d.x;
              mp[n].y = (float)val.three_d.y;
            } else {
              mp[n].x = (float)val.two_d.x;
              mp[n].y = (float)val.two_d.y;
            }
            n++;
          }
        }
        gh->motionN = n;
      }
    } else {
      gh->status |= 1;
    }
  }

  return err;
}

static void DbgLog(const char *fmt, ...);

static PF_Err BakeTextOutlines(Suites *suites, PF_InData *in_data,
                               AEGP_LayerH layerH, const A_Time *layerT, BakeCtx *cx)
{
  PF_Err err = PF_Err_NONE;
  if (!suites->lay || !suites->txt || !suites->str) return err;
  AEGP_ObjectType objType = AEGP_ObjectType_AV;
  if (suites->lay->AEGP_GetLayerObjectType(layerH, &objType)) return err;
  if (objType != AEGP_ObjectType_TEXT) return err;
  ((GeoHeader *)cx->blob->base)->status |= 4 | 16; // comp-space buffer (vector layer) + text layer

  AEGP_TextOutlinesH outlinesH = NULL;
  A_Time t = *layerT;
  A_Err te = suites->txt->AEGP_GetNewTextOutlines(layerH, &t, &outlinesH);
  if (te) return err;
  if (!outlinesH) return err;

  // PF path data suite for reading the outlines
  PF_PathDataSuite1 *pds = NULL;
  A_long nOut = 0;
  A_Err eNum = suites->txt->AEGP_GetNumTextOutlines(outlinesH, &nOut);
  DbgLog("text: nOut=%ld\n", (long)nOut);
  if (!eNum && nOut > 0) {
    A_Err eAcq = in_data->pica_basicP->AcquireSuite(kPFPathDataSuite, kPFPathDataSuiteVersion1,
                                            (const void **)&pds);
    if (!eAcq && pds) {
      for (A_long i = 0; i < nOut; i++) {
        PF_PathOutlinePtr pathP = NULL;
        A_Err eIdx = suites->txt->AEGP_GetIndexedTextOutline(outlinesH, i, &pathP);
        if (eIdx) { DbgLog("text outline idx %ld err=%d (nOut=%ld)\n", (long)i, (int)eIdx, (long)nOut); continue; }
        if (!pathP) continue;
        PF_Boolean openB = TRUE;
        A_long nSegs = 0;
        A_Err eOpen = pds->PF_PathIsOpen(in_data->effect_ref, pathP, &openB);
        if (eOpen) continue;
        A_Err eSeg = pds->PF_PathNumSegments(in_data->effect_ref, pathP, &nSegs);
        if (eSeg) continue;
        // PF_PathVertexInfo is valid for [0..nSegs] for both open and closed
        // outlines. The old code read an uninitialized closed-path endpoint and
        // then overwrote it, which could lose the closing glyph handle in mixed
        // CJK/Latin runs. Keep the endpoint returned by AE and validate every
        // vertex before appending the outline.
        A_long nV = nSegs + 1;
        if (nV < 2 || nV > 100000) continue;
        PF_PathVertex *verts = (PF_PathVertex *)calloc((size_t)nV, sizeof(PF_PathVertex));
        if (!verts) continue;
        bool vertsOK = true;
        for (A_long vi = 0; vi < nV; vi++) {
          A_Err ev = pds->PF_PathVertexInfo(in_data->effect_ref, pathP, vi, &verts[vi]);
          if (ev) {
            DbgLog("text outline idx %ld vertex %ld/%ld err=%d\n",
                   (long)i, (long)vi, (long)nSegs, (int)ev);
            vertsOK = false;
            break;
          }
        }
        if (vertsOK) {
          // Text outline tangents are absolute handle positions; convert to
          // vertex-relative offsets (mask paths already use this convention).
          for (A_long vi = 0; vi < nV; vi++) {
            PF_FpLong rawInX = verts[vi].tan_in_x, rawInY = verts[vi].tan_in_y;
            PF_FpLong rawOutX = verts[vi].tan_out_x, rawOutY = verts[vi].tan_out_y;
            verts[vi].tan_in_x  -= verts[vi].x;
            verts[vi].tan_in_y  -= verts[vi].y;
            verts[vi].tan_out_x -= verts[vi].x;
            verts[vi].tan_out_y -= verts[vi].y;
            if (vi < 6) DbgLog("text idx=%ld vi=%ld p=(%.2f,%.2f) rawIn=(%.2f,%.2f) rawOut=(%.2f,%.2f) relIn=(%.2f,%.2f) relOut=(%.2f,%.2f)\n",
                               (long)i, (long)vi, (double)verts[vi].x, (double)verts[vi].y,
                               (double)rawInX, (double)rawInY, (double)rawOutX, (double)rawOutY,
                               (double)verts[vi].tan_in_x, (double)verts[vi].tan_in_y,
                               (double)verts[vi].tan_out_x, (double)verts[vi].tan_out_y);
          }
          // Text is intentionally not thinned: every glyph, including CJK
          // glyphs adjacent to Latin glyphs, contributes all points/handles.
          AppendPathDataEx(cx, verts, nSegs, !openB, true);
        }
        free(verts);
      }
      in_data->pica_basicP->ReleaseSuite(kPFPathDataSuite, kPFPathDataSuiteVersion1);
    }
  }
  suites->txt->AEGP_DisposeTextOutlines(outlinesH);
  return err;
}

struct ChildSig {
  A_long total, nGroup, nOneD, nTwoD, nSpatial, nColor, nMask;
};
static void GetChildSig(Suites *suites, AEGP_StreamRefH groupH, ChildSig *s);
static bool StreamNameEN(Suites *suites, AEGP_StreamRefH sH, char *buf, size_t cap);

/* the transform group of a shape group sits inside its Contents (indexed) group,
   not as a direct named child — find it by its leaf signature
   (anchor+position spatial, scale 2D, rotation/skew/axis/opacity 1D).
   Only indexed groups are transparent to the search; a named child is a logical
   sub-group with its own transform and must not leak its transform upward. */
static AEGP_StreamRefH FindTransformGroup(Suites *suites, AEGP_StreamRefH groupH, int depth)
{
  if (depth > 2) return NULL;
  A_long n = 0;
  if (suites->dyn->AEGP_GetNumStreamsInGroup(groupH, &n)) return NULL;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH childH = NULL;
    if (suites->dyn->AEGP_GetNewStreamRefByIndex(0, groupH, i, &childH) || !childH)
      continue;
    AEGP_StreamGroupingType gt = AEGP_StreamGroupingType_LEAF;
    suites->dyn->AEGP_GetStreamGroupingType(childH, &gt);
    if (gt != AEGP_StreamGroupingType_LEAF) {
      ChildSig s;
      GetChildSig(suites, childH, &s);
      if (s.nGroup == 0 && s.total == 7 && s.nSpatial == 2 && s.nTwoD == 1 && s.nOneD == 4)
        return childH; // caller disposes
      if (gt == AEGP_StreamGroupingType_INDEXED_GROUP) {
        AEGP_StreamRefH found = FindTransformGroup(suites, childH, depth + 1);
        if (found) {
          suites->str->AEGP_DisposeStream(childH);
          return found;
        }
      }
    }
    suites->str->AEGP_DisposeStream(childH);
  }
  return NULL;
}

/* find a direct child by its force-English display name (probe-free) */
static AEGP_StreamRefH FindChildByNameEN(Suites *suites, AEGP_StreamRefH groupH, const char *nameEN)
{
  A_long n = 0;
  if (suites->dyn->AEGP_GetNumStreamsInGroup(groupH, &n)) return NULL;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH cH = NULL;
    if (suites->dyn->AEGP_GetNewStreamRefByIndex(0, groupH, i, &cH) || !cH) continue;
    char nm[64];
    nm[0] = 0;
    if (StreamNameEN(suites, cH, nm, sizeof(nm)) && !strcmp(nm, nameEN))
      return cH; // caller disposes
    suites->str->AEGP_DisposeStream(cH);
  }
  return NULL;
}

/* group transform streams -> Mat (leaf values at current time) */
static Mat ReadGroupXform(Suites *suites, PF_InData *in_data,
                          AEGP_StreamRefH groupH, A_Time *t)
{
  Mat m = MatId();
  AEGP_StreamRefH xgH = FindTransformGroup(suites, groupH, 0);
  if (!xgH) {
    return m;
  }
  double ax = 0, ay = 0, px = 0, py = 0, sx = 100, sy = 100, rot = 0, skew = 0, skewA = 0;
  AEGP_StreamRefH leafH = NULL;
  AEGP_StreamValue2 val;
  const char *names[] = {"Anchor Point", "Position", "Scale",
                         "Rotation", "Skew", "Skew Axis"};
  for (int i = 0; i < 6; i++) {
    leafH = FindChildByNameEN(suites, xgH, names[i]);
    if (!leafH)
      continue;
    AEGP_StreamType st = AEGP_StreamType_NO_DATA;
    suites->str->AEGP_GetStreamType(leafH, &st);
    if (!suites->str->AEGP_GetNewStreamValue(0, leafH, AEGP_LTimeMode_LayerTime, t, FALSE, &val)) {
      bool spatial = (st == AEGP_StreamType_ThreeD || st == AEGP_StreamType_ThreeD_SPATIAL ||
                      st == AEGP_StreamType_TwoD_SPATIAL);
      double vx = spatial ? val.val.three_d.x : (st == AEGP_StreamType_OneD ? val.val.one_d : val.val.two_d.x);
      double vy = spatial ? val.val.three_d.y : (st == AEGP_StreamType_OneD ? 0.0 : val.val.two_d.y);
      switch (i) {
        case 0: ax = vx; ay = vy; break;
        case 1: px = vx; py = vy; break;
        case 2: sx = vx; sy = vy; break;
        case 3: rot = vx; break;
        case 4: skew = vx; break;
        case 5: skewA = vx; break;
      }
      suites->str->AEGP_DisposeStreamValue(&val);
    }
    suites->str->AEGP_DisposeStream(leafH);
  }
  suites->str->AEGP_DisposeStream(xgH);

  m = MatTrans(px, py);
  m = MatMul(m, MatRot(rot));
  m = MatMul(m, MatSkew(skew, skewA));
  m = MatMul(m, MatScale(sx / 100.0, sy / 100.0));
  m = MatMul(m, MatTrans(-ax, -ay));
  return m;
}

struct ShapeAncestors;
static void AppendShapeMotionPath(Suites *suites, PF_InData *in_data,
                                  AEGP_LayerH layerH, const ShapeAncestors *anc,
                                  AEGP_StreamRefH shapeH, const char *kind,
                                  AEGP_StreamRefH pathStreamH, A_long structurePathIndex,
                                  BakeCtx *cx);

static void BakeBezierStream(Suites *suites, PF_InData *in_data,
                             AEGP_LayerH layerH, const ShapeAncestors *anc,
                             AEGP_StreamRefH pathStreamH, const Mat &m, A_Time *t, BakeCtx *cx)
{
  AEGP_StreamValue2 val;
  if (suites->str->AEGP_GetNewStreamValue(0, pathStreamH, AEGP_LTimeMode_LayerTime, t, FALSE, &val))
    return;
  A_long nSegs = 0; bool closed = false;
  PF_PathVertex *verts = ReadOutlineVerts(suites, val.val.mask, &nSegs, &closed);
  if (verts) {
    A_long nV = nSegs + 1; // verts array always has nSegs+1 entries (closure dup for closed)
    for (A_long i = 0; i < nV; i++) {
      Pt p = MatApply(m, verts[i].x, verts[i].y);
      Pt ti = MatApply(m, verts[i].x + verts[i].tan_in_x, verts[i].y + verts[i].tan_in_y);
      Pt to = MatApply(m, verts[i].x + verts[i].tan_out_x, verts[i].y + verts[i].tan_out_y);
      verts[i].x = p.x; verts[i].y = p.y;
      verts[i].tan_in_x = ti.x - p.x; verts[i].tan_in_y = ti.y - p.y;
      verts[i].tan_out_x = to.x - p.x; verts[i].tan_out_y = to.y - p.y;
    }
    AppendPathData(cx, verts, nSegs, closed);
    if (cx->needMotion)
      AppendShapeMotionPath(suites, in_data, layerH, anc, NULL, NULL, pathStreamH,
                            ((GeoHeader *)cx->blob->base)->pathCount - 1, cx);
    free(verts);
  }
  suites->str->AEGP_DisposeStreamValue(&val);
}

/* probe-free stream identification. GetNewStreamRefByMatchname with a name
   that is not a legal child of the parent trips AE's internal verification
   ("{match_name … is not a legal child of the passed parent_group}"), which
   shows up as the GUI "内部验证故障" dialog — so children are always
   enumerated by index and identified by stream-type signature or by the
   (force-English) display name instead. */
static void GetChildSig(Suites *suites, AEGP_StreamRefH groupH, ChildSig *s)
{
  memset(s, 0, sizeof(*s));
  A_long n = 0;
  if (suites->dyn->AEGP_GetNumStreamsInGroup(groupH, &n)) return;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH cH = NULL;
    if (suites->dyn->AEGP_GetNewStreamRefByIndex(0, groupH, i, &cH) || !cH) continue;
    AEGP_StreamGroupingType gt = AEGP_StreamGroupingType_LEAF;
    suites->dyn->AEGP_GetStreamGroupingType(cH, &gt);
    s->total++;
    if (gt == AEGP_StreamGroupingType_LEAF) {
      AEGP_StreamType st = AEGP_StreamType_NO_DATA;
      suites->str->AEGP_GetStreamType(cH, &st);
      if (st == AEGP_StreamType_OneD) s->nOneD++;
      else if (st == AEGP_StreamType_TwoD) s->nTwoD++;
      else if (st == AEGP_StreamType_TwoD_SPATIAL || st == AEGP_StreamType_ThreeD ||
               st == AEGP_StreamType_ThreeD_SPATIAL) s->nSpatial++;
      else if (st == AEGP_StreamType_COLOR) s->nColor++;
      else if (st == AEGP_StreamType_MASK) s->nMask++;
    } else {
      s->nGroup++;
    }
    suites->str->AEGP_DisposeStream(cH);
  }
}

/* force-English display name of a stream (ASCII subset) */
static bool StreamNameEN(Suites *suites, AEGP_StreamRefH sH, char *buf, size_t cap)
{
  if (!suites->mem) return false;
  AEGP_MemHandle h = NULL;
  if (suites->str->AEGP_GetStreamName(0, sH, TRUE, &h) || !h) return false;
  bool ok = false;
  AEGP_MemSize sz = 0;
  if (!suites->mem->AEGP_GetMemHandleSize(h, &sz) && sz > 0) {
    A_UTF16Char *p = NULL;
    if (!suites->mem->AEGP_LockMemHandle(h, (void **)&p) && p) {
      size_t w = 0;
      while (w + 1 < cap && p[w] && p[w] < 128) { buf[w] = (char)p[w]; w++; }
      buf[w] = 0;
      ok = (w > 0);
      suites->mem->AEGP_UnlockMemHandle(h);
    }
  }
  suites->mem->AEGP_FreeMemHandle(h);
  return ok;
}

/* read one numeric leaf (one_d / two_d / spatial) inside a parametric shape group,
   located by force-English display name (probe-free).
   spatial 2D streams (TwoD_SPATIAL) carry their x/y in the three_d union member —
   reading two_d yields zeros/garbage, so dispatch on the actual stream type. */
static void ReadLeafVal(Suites *suites, AEGP_StreamRefH groupH,
                        const char *nameEN, A_Time *t, double *x, double *y)
{
  AEGP_StreamRefH leafH = FindChildByNameEN(suites, groupH, nameEN);
  if (!leafH)
    return;
  AEGP_StreamType st = AEGP_StreamType_NO_DATA;
  suites->str->AEGP_GetStreamType(leafH, &st);
  AEGP_StreamValue2 val;
  A_Err e = suites->str->AEGP_GetNewStreamValue(0, leafH, AEGP_LTimeMode_LayerTime, t, FALSE, &val);
  if (!e) {
    if (st == AEGP_StreamType_ThreeD || st == AEGP_StreamType_ThreeD_SPATIAL ||
        st == AEGP_StreamType_TwoD_SPATIAL) {
      *x = val.val.three_d.x;
      *y = val.val.three_d.y;
    } else if (st == AEGP_StreamType_OneD) {
      *x = val.val.one_d;
    } else {
      *x = val.val.two_d.x;
      *y = val.val.two_d.y;
    }
    suites->str->AEGP_DisposeStreamValue(&val);
  }
  suites->str->AEGP_DisposeStream(leafH);
}

#define SHAPE_ANCESTOR_MAX 16
struct ShapeAncestors {
  AEGP_StreamRefH refs[SHAPE_ANCESTOR_MAX];
  A_long n;
};

static Mat ShapeMatrixAtComp(Suites *suites, PF_InData *in_data,
                             AEGP_LayerH layerH, const ShapeAncestors *anc,
                             const A_Time *compT)
{
  LayerX lx;
  A_Time compCopy = *compT;
  ReadLayerXform(suites, layerH, &compCopy, &lx);
  Mat m = MatFromLayerX(&lx);
  A_Time layerT = LayerTimeAtComp(suites, layerH, compT);
  for (A_long i = 0; i < anc->n; i++)
    m = MatMul(m, ReadGroupXform(suites, in_data, anc->refs[i], &layerT));
  return m;
}

static bool ReadPathCentroid(Suites *suites, AEGP_StreamRefH pathStreamH,
                             const A_Time *layerT, Pt *center)
{
  AEGP_StreamValue2 val;
  if (suites->str->AEGP_GetNewStreamValue(0, pathStreamH, AEGP_LTimeMode_LayerTime,
                                          layerT, FALSE, &val)) return false;
  A_long nSegs = 0; bool closed = false;
  PF_PathVertex *verts = ReadOutlineVerts(suites, val.val.mask, &nSegs, &closed);
  suites->str->AEGP_DisposeStreamValue(&val);
  if (!verts || nSegs < 1) { if (verts) free(verts); return false; }
  A_long nV = closed ? nSegs : nSegs + 1;
  double sx = 0, sy = 0;
  for (A_long i = 0; i < nV; i++) { sx += verts[i].x; sy += verts[i].y; }
  center->x = (float)(sx / nV); center->y = (float)(sy / nV);
  free(verts);
  return true;
}

static bool ReadParamShapeCenter(Suites *suites, AEGP_StreamRefH shapeH,
                                 const char *kind, const A_Time *layerT, Pt *center)
{
  double x = 0, y = 0;
  ReadLeafVal(suites, shapeH, "Position", (A_Time *)layerT, &x, &y);
  center->x = (float)x; center->y = (float)y;
  return true;
}

static A_long AppendOccluderQuad(BakeCtx *cx, const Pt *quad);

static A_long AppendPathBoundsOccluder(BakeCtx *cx, A_long pathIndex)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  if (pathIndex < 0 || pathIndex >= gh->pathCount || gh->occCount >= GEO_MAX_OCC) return -1;
  PathDesc *paths = (PathDesc *)(cx->blob->base + cx->pdBase);
  const PathDesc *pd = &paths[pathIndex];
  if (pd->kind != 0 || pd->n < 2) return -1;
  const Pt *flat = (const Pt *)(cx->blob->base + cx->flatStart);
  float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
  for (A_long i = pd->off; i < pd->off + pd->n; i++) {
    if (flat[i].x < bx0) bx0 = flat[i].x;
    if (flat[i].y < by0) by0 = flat[i].y;
    if (flat[i].x > bx1) bx1 = flat[i].x;
    if (flat[i].y > by1) by1 = flat[i].y;
  }
  if (bx1 < bx0 || by1 < by0) return -1;
  Pt q[4] = {{bx0, by0}, {bx1, by0}, {bx1, by1}, {bx0, by1}};
  return AppendOccluderQuad(cx, q);
}

/* Per-shape motion path. Layer Position alone is insufficient when multiple
   vector items animate inside one shape layer, so sample each item's own path
   centroid/parametric position and its complete ancestor transform chain. */
static void AppendShapeMotionPath(Suites *suites, PF_InData *in_data,
                                  AEGP_LayerH layerH, const ShapeAncestors *anc,
                                  AEGP_StreamRefH shapeH, const char *kind,
                                  AEGP_StreamRefH pathStreamH, A_long structurePathIndex,
                                  BakeCtx *cx)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  Blob *b = cx->blob;
  if (gh->pathCount >= GEO_MAX_PATHS) return;
  A_long off = (b->len - cx->flatStart) / (A_long)sizeof(Pt);
  double dur = NowSec(in_data, in_data->total_time);
  double x0 = 0, y0 = 0, maxDev = 0;
  A_long n = 0;
  for (A_long i = 0; i <= MOTION_SAMPLES; i++) {
    double sec = dur * (double)i / MOTION_SAMPLES;
    A_Time compT; TimeOf(in_data, sec, &compT);
    A_Time layerT = LayerTimeAtComp(suites, layerH, &compT);
    Pt local;
    bool ok = pathStreamH ? ReadPathCentroid(suites, pathStreamH, &layerT, &local)
                          : ReadParamShapeCenter(suites, shapeH, kind, &layerT, &local);
    if (!ok) break;
    Mat m = ShapeMatrixAtComp(suites, in_data, layerH, anc, &compT);
    Pt p = MatApply(m, local.x, local.y);
    if (i == 0) { x0 = p.x; y0 = p.y; }
    double dev = fabs((double)p.x - x0) + fabs((double)p.y - y0);
    if (dev > maxDev) maxDev = dev;
    Pt *dst = (Pt *)BlobAt(b, sizeof(Pt));
    if (!dst) return;
    *dst = p; n++;
  }
  if (n < 2 || maxDev < 0.5) {
    b->len = cx->flatStart + off * (A_long)sizeof(Pt);
    return;
  }
  PathDesc *pd = (PathDesc *)(b->base + cx->pdBase + gh->pathCount * sizeof(PathDesc));
  gh->pathCount++;
  pd->off = off; pd->n = n; pd->closed = 0; pd->kind = 1;
  pd->occ = AppendPathBoundsOccluder(cx, structurePathIndex);
}

static void BakeParamShape(Suites *suites, PF_InData *in_data,
                           AEGP_LayerH layerH, const ShapeAncestors *anc,
                           AEGP_StreamRefH shapeH, const char *kind, const Mat &m,
                           A_Time *t, BakeCtx *cx)
{
  const double KAPPA = 0.5523;
  const A_long MAX_PARAM_VERTS = 1024;
  PF_PathVertex *v = (PF_PathVertex *)calloc((size_t)(MAX_PARAM_VERTS + 1), sizeof(PF_PathVertex));
  if (!v) return;
  A_long nV = 0;
  double y0 = 0;

  if (!strcmp(kind, "rect")) {
    double w = 100, h = 100, px = 0, py = 0, rnd = 0;
    ReadLeafVal(suites, shapeH, "Size", t, &w, &h);
    ReadLeafVal(suites, shapeH, "Position", t, &px, &py);
    ReadLeafVal(suites, shapeH, "Roundness", t, &rnd, &y0);
    double hw = w / 2, hh = h / 2;
    double r = rnd < 0 ? 0 : (rnd > (hw < hh ? hw : hh) ? (hw < hh ? hw : hh) : rnd);
    double tt = r * KAPPA;
    double cx4[4] = {px + hw, px + hw, px - hw, px - hw};
    double cy4[4] = {py - hh, py + hh, py + hh, py - hh};
    double tin[4][2] = {{-tt, 0}, {0, -tt}, {tt, 0}, {0, tt}};
    double tout[4][2] = {{0, tt}, {-tt, 0}, {0, -tt}, {tt, 0}};
    for (int i = 0; i < 4; i++) {
      v[i].x = cx4[i]; v[i].y = cy4[i];
      v[i].tan_in_x = tin[i][0]; v[i].tan_in_y = tin[i][1];
      v[i].tan_out_x = tout[i][0]; v[i].tan_out_y = tout[i][1];
    }
    nV = 4;
  } else if (!strcmp(kind, "ellipse")) {
    double w = 100, h = 100, px = 0, py = 0;
    ReadLeafVal(suites, shapeH, "Size", t, &w, &h);
    ReadLeafVal(suites, shapeH, "Position", t, &px, &py);
    double rx = w / 2, ry = h / 2, tx = rx * KAPPA, ty = ry * KAPPA;
    double vx[4] = {px + rx, px, px - rx, px};
    double vy[4] = {py, py + ry, py, py - ry};
    double tin[4][2] = {{0, -ty}, {tx, 0}, {0, ty}, {-tx, 0}};
    double tout[4][2] = {{0, ty}, {-tx, 0}, {0, -ty}, {tx, 0}};
    for (int i = 0; i < 4; i++) {
      v[i].x = vx[i]; v[i].y = vy[i];
      v[i].tan_in_x = tin[i][0]; v[i].tan_in_y = tin[i][1];
      v[i].tan_out_x = tout[i][0]; v[i].tan_out_y = tout[i][1];
    }
    nV = 4;
  } else if (!strcmp(kind, "star")) {
    double stype = 1, nPts = 5, rot = 0, rIn = 50, rOut = 100, px = 0, py = 0;
    ReadLeafVal(suites, shapeH, "Star Type", t, &stype, &y0);
    ReadLeafVal(suites, shapeH, "Points", t, &nPts, &y0);
    ReadLeafVal(suites, shapeH, "Rotation", t, &rot, &y0);
    ReadLeafVal(suites, shapeH, "Inner Radius", t, &rIn, &y0);
    ReadLeafVal(suites, shapeH, "Outer Radius", t, &rOut, &y0);
    ReadLeafVal(suites, shapeH, "Position", t, &px, &py);
    bool isStar = ((int)stype == 1);
    A_long count = (A_long)(isStar ? nPts * 2 : nPts);
    if (count > MAX_PARAM_VERTS) count = MAX_PARAM_VERTS;
    for (A_long i = 0; i < count; i++) {
      double rr = isStar ? ((i % 2 == 0) ? rOut : rIn) : rOut;
      double ang = rot * M_PI / 180.0 - M_PI / 2 +
                   (double)i * (isStar ? M_PI / nPts : 2.0 * M_PI / nPts);
      v[i].x = px + rr * cos(ang);
      v[i].y = py + rr * sin(ang);
      v[i].tan_in_x = v[i].tan_in_y = v[i].tan_out_x = v[i].tan_out_y = 0;
    }
    nV = count;
  }
  if (nV < 3) { free(v); return; }

  // transform into layer space
  for (A_long i = 0; i < nV; i++) {
    Pt p = MatApply(m, v[i].x, v[i].y);
    Pt ti = MatApply(m, v[i].x + v[i].tan_in_x, v[i].y + v[i].tan_in_y);
    Pt to = MatApply(m, v[i].x + v[i].tan_out_x, v[i].y + v[i].tan_out_y);
    v[i].x = p.x; v[i].y = p.y;
    v[i].tan_in_x = ti.x - p.x; v[i].tan_in_y = ti.y - p.y;
    v[i].tan_out_x = to.x - p.x; v[i].tan_out_y = to.y - p.y;
  }
  // nSegs == nV for closed shapes; provide the explicit closing vertex.
  v[nV] = v[0];
  A_long structurePathIndex = ((GeoHeader *)cx->blob->base)->pathCount;
  AppendPathData(cx, v, nV, true);
  if (cx->needMotion)
    AppendShapeMotionPath(suites, in_data, layerH, anc, shapeH, kind, NULL,
                          structurePathIndex, cx);
  free(v);
}

/* bake every MASK-typed leaf of a group (custom bezier paths) */
static void BakeMaskLeaves(Suites *suites, PF_InData *in_data,
                           AEGP_LayerH layerH, const ShapeAncestors *anc,
                           AEGP_StreamRefH groupH, const Mat &m, A_Time *t, BakeCtx *cx)
{
  A_long n = 0;
  if (suites->dyn->AEGP_GetNumStreamsInGroup(groupH, &n)) return;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH cH = NULL;
    if (suites->dyn->AEGP_GetNewStreamRefByIndex(0, groupH, i, &cH) || !cH) continue;
    AEGP_StreamGroupingType gt = AEGP_StreamGroupingType_LEAF;
    suites->dyn->AEGP_GetStreamGroupingType(cH, &gt);
    if (gt == AEGP_StreamGroupingType_LEAF) {
      AEGP_StreamType st = AEGP_StreamType_NO_DATA;
      suites->str->AEGP_GetStreamType(cH, &st);
      if (st == AEGP_StreamType_MASK) BakeBezierStream(suites, in_data, layerH, anc, cH, m, t, cx);
    }
    suites->str->AEGP_DisposeStream(cH);
  }
}

static void WalkShapeGroup(Suites *suites, PF_InData *in_data,
                           AEGP_LayerH layerH, AEGP_StreamRefH groupH,
                           const Mat &parentM, A_Time *t, const ShapeAncestors *anc,
                           BakeCtx *cx, int depth)
{
  if (depth > 16) return;
  // only a named group is a logical shape group with its own transform;
  // indexed (Contents) groups are transparent containers
  AEGP_StreamGroupingType selfType = AEGP_StreamGroupingType_LEAF;
  suites->dyn->AEGP_GetStreamGroupingType(groupH, &selfType);
  Mat selfM = (selfType == AEGP_StreamGroupingType_NAMED_GROUP)
                  ? ReadGroupXform(suites, in_data, groupH, t)
                  : MatId();
  Mat m = MatMul(parentM, selfM);
  ShapeAncestors childAnc = *anc;
  if (selfType == AEGP_StreamGroupingType_NAMED_GROUP &&
      childAnc.n < SHAPE_ANCESTOR_MAX)
    childAnc.refs[childAnc.n++] = groupH;

  A_long n = 0;
  A_Err nErr = suites->dyn->AEGP_GetNumStreamsInGroup(groupH, &n);
  if (nErr) return;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH childH = NULL;
    A_Err cErr = suites->dyn->AEGP_GetNewStreamRefByIndex(0, groupH, i, &childH);
    if (cErr || !childH) continue;
    AEGP_StreamGroupingType gtype = AEGP_StreamGroupingType_LEAF;
    suites->dyn->AEGP_GetStreamGroupingType(childH, &gtype);

    if (gtype != AEGP_StreamGroupingType_LEAF) {
      // classify by child-leaf signature (probe-free)
      ChildSig s;
      GetChildSig(suites, childH, &s);
      if (getenv("EVIZ_DEBUG")) {
        FILE *fd = fopen("/tmp/ev_dbg.log", "a");
        if (fd) { fprintf(fd, "walk d=%d i=%ld sig: total=%ld grp=%ld 1d=%ld 2d=%ld sp=%ld col=%ld mask=%ld\n",
                          depth, (long)i, (long)s.total, (long)s.nGroup, (long)s.nOneD, (long)s.nTwoD,
                          (long)s.nSpatial, (long)s.nColor, (long)s.nMask); fclose(fd); }
      }
      if (s.nMask > 0) {
        BakeMaskLeaves(suites, in_data, layerH, &childAnc, childH, m, t, cx);
      } else if (s.nGroup == 0 && s.total == 4 && s.nTwoD == 1 && s.nSpatial == 1 && s.nOneD == 2 && s.nColor == 0) {
        BakeParamShape(suites, in_data, layerH, &childAnc, childH, "rect", m, t, cx);
      } else if (s.nGroup == 0 && s.total == 3 && s.nTwoD == 1 && s.nSpatial == 1 && s.nOneD == 1 && s.nColor == 0) {
        BakeParamShape(suites, in_data, layerH, &childAnc, childH, "ellipse", m, t, cx);
      } else if (s.nGroup == 0 && s.nOneD >= 5 && s.nSpatial == 1 && s.nTwoD == 0 && s.nColor == 0) {
        BakeParamShape(suites, in_data, layerH, &childAnc, childH, "star", m, t, cx);
      } else {
        // any other group (shape groups, contents, transform, fills…): recurse;
        // shape items inside are found by their own leaves, the rest is skipped
        WalkShapeGroup(suites, in_data, layerH, childH, m, t, &childAnc, cx, depth + 1);
      }
    } else {
      AEGP_StreamType vtype = AEGP_StreamType_NO_DATA;
      suites->str->AEGP_GetStreamType(childH, &vtype);
      if (vtype == AEGP_StreamType_MASK) {
        BakeBezierStream(suites, in_data, layerH, &childAnc, childH, m, t, cx);
      }
    }
    suites->str->AEGP_DisposeStream(childH);
  }
}

/* the shape tree root ("ADBE Root Vectors Group") is the layer child whose
   English display name is "Contents"; absent on non-shape layers */
static AEGP_StreamRefH FindRootVectorsGroup(Suites *suites, AEGP_StreamRefH rootH)
{
  A_long n = 0;
  if (suites->dyn->AEGP_GetNumStreamsInGroup(rootH, &n)) return NULL;
  for (A_long i = 0; i < n; i++) {
    AEGP_StreamRefH cH = NULL;
    if (suites->dyn->AEGP_GetNewStreamRefByIndex(0, rootH, i, &cH) || !cH) continue;
    AEGP_StreamGroupingType gt = AEGP_StreamGroupingType_LEAF;
    suites->dyn->AEGP_GetStreamGroupingType(cH, &gt);
    char nm[64];
    nm[0] = 0;
    bool got = StreamNameEN(suites, cH, nm, sizeof(nm));
    if (getenv("EVIZ_DEBUG")) {
      FILE *fd = fopen("/tmp/ev_dbg.log", "a");
      if (fd) { fprintf(fd, "root child %ld gt=%d got=%d name=[%s]\n", (long)i, (int)gt, (int)got, nm); fclose(fd); }
    }
    if (gt != AEGP_StreamGroupingType_LEAF && got && !strcmp(nm, "Contents"))
      return cH; // caller disposes
    suites->str->AEGP_DisposeStream(cH);
  }
  return NULL;
}

static PF_Err BakeShapes(Suites *suites, PF_InData *in_data,
                         AEGP_LayerH layerH, const A_Time *layerT,
                         const Mat &root, BakeCtx *cx)
{
  PF_Err err = PF_Err_NONE;
  if (!suites->dyn || !suites->str) { ((GeoHeader *)cx->blob->base)->status |= 2; return err; }
  AEGP_StreamRefH rootH = NULL;
  A_Err e1 = suites->dyn->AEGP_GetNewStreamRefForLayer(0, layerH, &rootH);
  AEGP_StreamRefH vecH = NULL;
  if (!e1 && rootH) {
    vecH = FindRootVectorsGroup(suites, rootH);
  }
  if (e1 || !rootH) {
    ((GeoHeader *)cx->blob->base)->status |= 2;
    return err;
  }
  if (!vecH) {
    ((GeoHeader *)cx->blob->base)->status |= 2;
    suites->str->AEGP_DisposeStream(rootH);
    return err;
  }
  // Shape streams are evaluated in layer time, while root maps layer space
  // into the caller's comp space.
  ((GeoHeader *)cx->blob->base)->status |= 4;
  ShapeAncestors anc; memset(&anc, 0, sizeof(anc));
  WalkShapeGroup(suites, in_data, layerH, vecH, root, (A_Time *)layerT, &anc, cx, 0);
  suites->str->AEGP_DisposeStream(vecH);
  suites->str->AEGP_DisposeStream(rootH);
  return err;
}

static void BakeLayerMaskSilhouette(Suites *suites, PF_InData *in_data,
                                    AEGP_LayerH L, const Mat &m, A_Time *t,
                                    BakeCtx *cx, float *silBBox);

static void AppendFlatPath(BakeCtx *cx, const Pt *pts, A_long n, bool closed, A_long kind);

/* Recursively inspect precomp contents instead of treating a precomp as an
   opaque footage rectangle. All child geometry is transformed through the
   child layer and then through the precomp layer into the caller's comp. */
static void BakeNestedCompContents(Suites *suites, PF_InData *in_data,
                                   AEGP_LayerH precompLayer, AEGP_ItemH sourceItem,
                                   const A_Time *outerCompT, const Mat &precompToOuter,
                                   BakeCtx *cx, int depth)
{
  if (depth > 4 || !suites->comp || !suites->lay || !suites->item || !sourceItem) return;
  AEGP_ItemType itemType = AEGP_ItemType_NONE;
  if (suites->item->AEGP_GetItemType(sourceItem, &itemType) ||
      itemType != AEGP_ItemType_COMP) return;
  AEGP_CompH nestedComp = NULL;
  if (suites->comp->AEGP_GetCompFromItem(sourceItem, &nestedComp) || !nestedComp) return;
  A_Time nestedCompT = LayerTimeAtComp(suites, precompLayer, outerCompT);
  A_long nL = 0;
  if (suites->lay->AEGP_GetCompNumLayers(nestedComp, &nL)) return;

  for (A_long idx = 0; idx < nL; idx++) {
    AEGP_LayerH child = NULL;
    if (suites->lay->AEGP_GetCompLayerByIndex(nestedComp, idx, &child) || !child) continue;
    AEGP_LayerFlags fl = AEGP_LayerFlag_NONE;
    if (suites->lay->AEGP_GetLayerFlags(child, &fl)) continue;
    if (!(fl & AEGP_LayerFlag_VIDEO_ACTIVE)) continue;
    if (fl & (AEGP_LayerFlag_ADJUSTMENT_LAYER | AEGP_LayerFlag_GUIDE_LAYER |
              AEGP_LayerFlag_NULL_LAYER)) continue;
    AEGP_ObjectType ot = AEGP_ObjectType_AV;
    if (suites->lay->AEGP_GetLayerObjectType(child, &ot)) continue;
    if (ot == AEGP_ObjectType_LIGHT || ot == AEGP_ObjectType_CAMERA) continue;
    if (fl & AEGP_LayerFlag_LAYER_IS_3D) continue; // nested 3D needs a nested camera

    LayerX childX;
    ReadLayerXform(suites, child, &nestedCompT, &childX);
    Mat childToOuter = MatMul(precompToOuter, MatFromLayerX(&childX));
    GeoHeader *gh = (GeoHeader *)cx->blob->base;
    A_long pathBefore = gh->pathCount;
    AEGP_ItemH childSource = NULL;
    AEGP_ItemType childSourceType = AEGP_ItemType_NONE;
    if (!suites->lay->AEGP_GetLayerSourceItem(child, &childSource) && childSource)
      suites->item->AEGP_GetItemType(childSource, &childSourceType);

    BakeLayerMaskSilhouette(suites, in_data, child, childToOuter, &nestedCompT, cx, NULL);
    A_Time childLayerT = LayerTimeAtComp(suites, child, &nestedCompT);
    BakeTextOutlines(suites, in_data, child, &childLayerT, cx);
    BakeShapes(suites, in_data, child, &childLayerT, childToOuter, cx);

    // A precomp child may be a solid/footage layer rather than a vector layer.
    // Still expose its visible edge as a drill-down path instead of treating
    // the precomp as a single opaque rectangle.
    bool childHasStructure = false;
    for (A_long pi = pathBefore; pi < gh->pathCount; pi++) {
      PathDesc *pd = (PathDesc *)(cx->blob->base + cx->pdBase) + pi;
      if (pd->kind == 0) { childHasStructure = true; break; }
    }
    if (!childHasStructure && childSource && suites->item) {
      A_long cw = 0, ch = 0;
      if (!suites->item->AEGP_GetItemDimensions(childSource, &cw, &ch) && cw > 0 && ch > 0) {
        Pt q[4] = {MatApply(childToOuter, 0, 0),
                   MatApply(childToOuter, (double)cw, 0),
                   MatApply(childToOuter, (double)cw, (double)ch),
                   MatApply(childToOuter, 0, (double)ch)};
        AppendFlatPath(cx, q, 4, true, 0);
      }
    }

    AEGP_ItemH grandSource = NULL;
    if (!suites->lay->AEGP_GetLayerSourceItem(child, &grandSource) && grandSource) {
      AEGP_ItemType grandType = AEGP_ItemType_NONE;
      if (!suites->item->AEGP_GetItemType(grandSource, &grandType) &&
          grandType == AEGP_ItemType_COMP)
        BakeNestedCompContents(suites, in_data, child, grandSource, &nestedCompT,
                               childToOuter, cx, depth + 1);
    }

    if (gh->pathCount > pathBefore && gh->frameN < GEO_MAX_FRAMES) {
      const Pt *flat = (const Pt *)(cx->blob->base + cx->flatStart);
      A_long currentFlatTotal = (cx->blob->len - cx->flatStart) / (A_long)sizeof(Pt);
      float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
      bool have = false;
      for (A_long pi = pathBefore; pi < gh->pathCount; pi++) {
        const PathDesc *pd = (const PathDesc *)(cx->blob->base + cx->pdBase) + pi;
        A_long cut = pd->kind == 1 ? MotionPrefixCount(in_data, gh->t0, gh->t1, pd->n) : pd->n;
        for (A_long i = pd->off; i < pd->off + cut && i < currentFlatTotal; i++) {
          if (flat[i].x < bx0) bx0 = flat[i].x;
          if (flat[i].y < by0) by0 = flat[i].y;
          if (flat[i].x > bx1) bx1 = flat[i].x;
          if (flat[i].y > by1) by1 = flat[i].y;
          have = true;
        }
      }
      if (have) {
        Pt *fq = (Pt *)(cx->blob->base + cx->frameBase + gh->frameN * 4 * sizeof(Pt));
        fq[0].x = bx0; fq[0].y = by0; fq[1].x = bx1; fq[1].y = by0;
        fq[2].x = bx1; fq[2].y = by1; fq[3].x = bx0; fq[3].y = by1;
        gh->frameN++;
      }
    }
  }
}

/* ================= geometry acquisition (render-time, serialized) ================= */

static Blob *BlobNew(A_long cap)
{
  Blob *b = (Blob *)malloc(sizeof(Blob));
  if (!b) return NULL;
  b->base = (char *)malloc(cap);
  if (!b->base) { free(b); return NULL; }
  b->cap = cap;
  b->len = 0;
  b->err = PF_Err_NONE;
  return b;
}
static void BlobFree(Blob *b)
{
  if (!b) return;
  if (b->base) free(b->base);
  free(b);
}

#define GEO_BLOB_CAP (8L * 1024L * 1024L)

/* append raw (already transformed) points as a path; no key vertices */
static void AppendFlatPath(BakeCtx *cx, const Pt *pts, A_long n, bool closed, A_long kind)
{
  Blob *b = cx->blob;
  GeoHeader *gh = (GeoHeader *)b->base;
  if (n < 2 || gh->pathCount >= GEO_MAX_PATHS) return;
  if (b->err) return;
  if (b->len + n * (A_long)sizeof(Pt) > b->cap) { b->err = PF_Err_OUT_OF_MEMORY; return; }
  PathDesc *pd = (PathDesc *)(b->base + cx->pdBase + gh->pathCount * sizeof(PathDesc));
  A_long off = (b->len - cx->flatStart) / (A_long)sizeof(Pt);
  memcpy(b->base + b->len, pts, n * sizeof(Pt));
  b->len += n * (A_long)sizeof(Pt);
  gh->pathCount++;
  pd->off = off; pd->n = n; pd->closed = closed ? 1 : 0; pd->kind = kind; pd->occ = -1;
}

/* Append the visible-content quad used to keep a motion path behind its own
   moving layer. It is deliberately separate from the visual frame: the frame
   may grow with travelled motion, while this quad remains the object's current
   occlusion region. */
static A_long AppendOccluderQuad(BakeCtx *cx, const Pt *quad)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  if (gh->occCount >= GEO_MAX_OCC) return -1;
  OccQuad *oq = (OccQuad *)(cx->blob->base + gh->occBase +
                            gh->occCount * (A_long)sizeof(OccQuad));
  memcpy(oq->p, quad, sizeof(oq->p));
  return gh->occCount++;
}

/* ---------- merged mask silhouette (rasterize + marching squares) ----------
   per user spec: a masked layer shows only the final merged outline; no vertex
   dots / bezier handles (they are noise on masked solids) */

struct LoopArr { Pt *pts; A_long n, cap; A_long mode; bool invert; };

static bool LoopPush(LoopArr *l, float x, float y)
{
  if (l->n >= l->cap) {
    A_long nextCap = l->cap ? l->cap * 2 : 256;
    if (nextCap <= l->cap || nextCap > (1L << 20)) return false;
    Pt *next = (Pt *)realloc(l->pts, sizeof(Pt) * (size_t)nextCap);
    if (!next) return false;
    l->pts = next;
    l->cap = nextCap;
  }
  l->pts[l->n].x = x; l->pts[l->n].y = y;
  l->n++;
  return true;
}

/* flatten one mask outline (bezier) into a closed point loop (layer space) */
static bool FlattenMaskOutline(Suites *suites, AEGP_MaskOutlineValH mo, LoopArr *out, bool *closedP)
{
  A_long nSegs = 0;
  bool closed = false;
  PF_PathVertex *verts = ReadOutlineVerts(suites, mo, &nSegs, &closed);
  if (!verts) return false;
  *closedP = closed;
  A_long nV = closed ? nSegs : nSegs + 1;
  if (nV < 2) { free(verts); return false; }
  for (A_long i = 0; i < nSegs; i++) {
    const PF_PathVertex *v0 = &verts[i];
    const PF_PathVertex *v1 = closed ? &verts[(i + 1) % nSegs] : &verts[i + 1];
    if (!LoopPush(out, (float)v0->x, (float)v0->y)) { free(verts); return false; }
    for (int k = 1; k <= FLAT_PER_SPAN; k++) {
      double tt = (double)k / FLAT_PER_SPAN, uu = 1.0 - tt;
      double px = uu*uu*uu*v0->x + 3*uu*uu*tt*(v0->x + v0->tan_out_x) +
                  3*uu*tt*tt*(v1->x + v1->tan_in_x) + tt*tt*tt*v1->x;
      double py = uu*uu*uu*v0->y + 3*uu*uu*tt*(v0->y + v0->tan_out_y) +
                  3*uu*tt*tt*(v1->y + v1->tan_in_y) + tt*tt*tt*v1->y;
      if (!LoopPush(out, (float)px, (float)py)) { free(verts); return false; }
    }
  }
  if (!closed && !LoopPush(out, (float)verts[nSegs].x, (float)verts[nSegs].y)) {
    free(verts); return false;
  }
  free(verts);
  return true;
}

/* even-odd toggle fill of one loop into a bitmap */
static void FillLoopXor(unsigned char *bmp, int bw, int bh, const LoopArr *l)
{
  float *xs = (float *)malloc(sizeof(float) * (l->n + 1));
  if (!xs) return;
  for (int y = 0; y < bh; y++) {
    float yc = (float)y + 0.5f;
    A_long nx = 0;
    for (A_long i = 0; i < l->n; i++) {
      float x0 = l->pts[i].x, y0 = l->pts[i].y;
      A_long j = (i + 1) % l->n;
      float x1 = l->pts[j].x, y1 = l->pts[j].y;
      if ((y0 <= yc && y1 > yc) || (y1 <= yc && y0 > yc)) {
        float tt = (yc - y0) / (y1 - y0);
        xs[nx++] = x0 + tt * (x1 - x0);
      }
    }
    for (int a = 1; a < nx; a++) {
      float v = xs[a]; int bIdx = a - 1;
      while (bIdx >= 0 && xs[bIdx] > v) { xs[bIdx + 1] = xs[bIdx]; bIdx--; }
      xs[bIdx + 1] = v;
    }
    for (int k = 0; k + 1 < nx; k += 2) {
      int xa = (int)floorf(xs[k]), xb = (int)ceilf(xs[k + 1]);
      if (xa < 0) xa = 0;
      if (xb > bw) xb = bw;
      for (int x = xa; x < xb; x++) bmp[y * bw + x] ^= 1;
    }
  }
  free(xs);
}

/* marching squares: trace all contour loops of a 0/1 bitmap.
   Grid nodes are pixel corners; cell (x,y) spans pixels x..x+1,y..y+1.
   Returns loops via callback-ish append into BakeCtx after transform. */
/* crossing point on the edge between two corner samples, with interp */
static inline void MsCross(float va, float vb, float x0, float y0, float x1, float y1, float *ox, float *oy)
{
  float tt = (0.5f - va) / (vb - va + 1e-9f);
  if (tt < 0) tt = 0; if (tt > 1) tt = 1;
  *ox = x0 + tt * (x1 - x0);
  *oy = y0 + tt * (y1 - y0);
}

static void TraceBitmapSilhouette(unsigned char *acc, int bw, int bh, float invS,
                                  float offX, float offY, const Mat &m, BakeCtx *cx)
{
  // build a scalar field of pixel-corner values from pixel-center fills:
  // corner (cx,cy) takes max of the 4 adjacent pixels → slightly dilated field,
  // which the 0.5 iso then hugs tightly around the silhouette.
  int gw = bw + 1, ghh = bh + 1;
  float *fld = (float *)malloc(sizeof(float) * gw * ghh);
  if (!fld) return;
  for (int y = 0; y < ghh; y++)
    for (int x = 0; x < gw; x++) {
      float v = 0;
      for (int dy = -1; dy <= 0; dy++)
        for (int dx = -1; dx <= 0; dx++) {
          int px = x + dx, py = y + dy;
          if (px >= 0 && px < bw && py >= 0 && py < bh && acc[py * bw + px]) v = 1.0f;
        }
      fld[y * gw + x] = v;
    }
  // chain marching-squares segments by shared edge nodes.
  // node id for the crossing on a vertical edge between corner (x,y)-(x,y+1):  x*ghh+y,   kind V
  //                                     horizontal edge (x,y)-(x+1,y):        x*ghh+y,   kind H
  // store segments as pairs of (kind,id) nodes; walk chains.
  A_long maxSeg = (A_long)gw * ghh * 2;
  A_long *segA = (A_long *)malloc(sizeof(A_long) * maxSeg);
  A_long *segB = (A_long *)malloc(sizeof(A_long) * maxSeg);
  float  *segAx = (float *)malloc(sizeof(float) * maxSeg);
  float  *segAy = (float *)malloc(sizeof(float) * maxSeg);
  float  *segBx = (float *)malloc(sizeof(float) * maxSeg);
  float  *segBy = (float *)malloc(sizeof(float) * maxSeg);
  if (!segA || !segB || !segAx || !segAy || !segBx || !segBy) {
    if (segA) free(segA); if (segB) free(segB); if (segAx) free(segAx);
    if (segAy) free(segAy); if (segBx) free(segBx); if (segBy) free(segBy);
    free(fld);
    return;
  }
  A_long nSeg = 0;
  for (int y = 0; y < ghh - 1; y++)
    for (int x = 0; x < gw - 1; x++) {
      float v0 = fld[y * gw + x], v1 = fld[y * gw + x + 1];
      float v2 = fld[(y + 1) * gw + x + 1], v3 = fld[(y + 1) * gw + x];
      int idx = (v0 > 0.5f ? 8 : 0) | (v1 > 0.5f ? 4 : 0) | (v2 > 0.5f ? 2 : 0) | (v3 > 0.5f ? 1 : 0);
      if (idx == 0 || idx == 15) continue;
      // edges: T(x,y top, H), R(x+1,y V), B(x,y+1 H), L(x,y V)
      float tX[4], tY[4];
      MsCross(v0, v1, (float)x, (float)y, (float)(x + 1), (float)y, &tX[0], &tY[0]); // T
      MsCross(v1, v2, (float)(x + 1), (float)y, (float)(x + 1), (float)(y + 1), &tX[1], &tY[1]); // R
      MsCross(v3, v2, (float)x, (float)(y + 1), (float)(x + 1), (float)(y + 1), &tX[2], &tY[2]); // B
      MsCross(v0, v3, (float)x, (float)y, (float)x, (float)(y + 1), &tX[3], &tY[3]); // L
      // node ids: H edge top of cell (x,y): id=(y*gw+x)*2; V edge left of cell: id=(y*gw+x)*2+1
      A_long nT = (A_long)(y * gw + x) * 2, nR = (A_long)(y * gw + x + 1) * 2 + 1;
      A_long nB = (A_long)((y + 1) * gw + x) * 2, nL = (A_long)(y * gw + x) * 2 + 1;
      int pairs[4][2];
      int np = 0;
      switch (idx) {
        case 1: case 14: pairs[np][0] = 3; pairs[np++][1] = 2; break;
        case 2: case 13: pairs[np][0] = 1; pairs[np++][1] = 2; break;
        case 3: case 12: pairs[np][0] = 3; pairs[np++][1] = 1; break;
        case 4: case 11: pairs[np][0] = 0; pairs[np++][1] = 1; break;
        case 5: pairs[np][0] = 3; pairs[np++][1] = 0; pairs[np][0] = 1; pairs[np++][1] = 2; break;
        case 6: case 9:  pairs[np][0] = 0; pairs[np++][1] = 2; break;
        case 7: case 8:  pairs[np][0] = 3; pairs[np++][1] = 0; break;
        case 10: pairs[np][0] = 0; pairs[np++][1] = 3; pairs[np][0] = 1; pairs[np++][1] = 2; break;
      }
      for (int k = 0; k < np && nSeg < maxSeg; k++) {
        int eA = pairs[k][0], eB = pairs[k][1];
        segA[nSeg] = (eA == 0) ? nT : (eA == 1) ? nR : (eA == 2) ? nB : nL;
        segB[nSeg] = (eB == 0) ? nT : (eB == 1) ? nR : (eB == 2) ? nB : nL;
        segAx[nSeg] = tX[eA]; segAy[nSeg] = tY[eA];
        segBx[nSeg] = tX[eB]; segBy[nSeg] = tY[eB];
        nSeg++;
      }
    }
  free(fld);
  if (nSeg == 0) { free(segA); free(segB); free(segAx); free(segAy); free(segBx); free(segBy); return; }

  // Chain segments into loops through a constant-time two-neighbour table.
  // A marching-squares edge has at most two incident contour segments; the
  // former full-array search made complex 768px silhouettes needlessly slow.
  bool *used = (bool *)malloc(sizeof(bool) * (size_t)nSeg);
  if (!used) { free(segA); free(segB); free(segAx); free(segAy); free(segBx); free(segBy); return; }
  memset(used, 0, sizeof(bool) * (size_t)nSeg);
  const A_long nodeCount = (A_long)gw * ghh * 2;
  A_long *nodeA = (A_long *)malloc(sizeof(A_long) * (size_t)nodeCount);
  A_long *nodeB = (A_long *)malloc(sizeof(A_long) * (size_t)nodeCount);
  if (nodeA && nodeB) {
    for (A_long i = 0; i < nodeCount; i++) { nodeA[i] = -1; nodeB[i] = -1; }
    for (A_long i = 0; i < nSeg; i++) {
      A_long a = segA[i], b = segB[i];
      if (a >= 0 && a < nodeCount) {
        if (nodeA[a] < 0) nodeA[a] = i;
        else if (nodeB[a] < 0) nodeB[a] = i;
      }
      if (b >= 0 && b < nodeCount) {
        if (nodeA[b] < 0) nodeA[b] = i;
        else if (nodeB[b] < 0) nodeB[b] = i;
      }
    }
  } else {
    if (nodeA) free(nodeA);
    if (nodeB) free(nodeB);
    nodeA = NULL; nodeB = NULL;
  }
  for (A_long s0 = 0; s0 < nSeg; s0++) {
    if (used[s0]) continue;
    LoopArr loop; loop.pts = NULL; loop.n = loop.cap = 0;
    used[s0] = true;
    if (!LoopPush(&loop, segAx[s0], segAy[s0])) {
      if (loop.pts) free(loop.pts);
      continue;
    }
    float cx1 = segBx[s0], cy1 = segBy[s0];
    bool loopOK = true;
    A_long curNode = segB[s0];
    A_long guard = nSeg + 4;
    while (guard-- > 0) {
      // Find an unused segment incident to the current node. The fallback
      // preserves behavior if the temporary adjacency table cannot allocate.
      A_long nxt = -1; bool flip = false;
      if (nodeA && nodeB && curNode >= 0 && curNode < nodeCount) {
        A_long qa = nodeA[curNode], qb = nodeB[curNode];
        if (qa >= 0 && qa < nSeg && !used[qa]) {
          nxt = qa; flip = (segA[qa] != curNode && segB[qa] == curNode);
        } else if (qb >= 0 && qb < nSeg && !used[qb]) {
          nxt = qb; flip = (segA[qb] != curNode && segB[qb] == curNode);
        }
      } else {
        for (A_long q = 0; q < nSeg; q++) {
          if (used[q]) continue;
          if (segA[q] == curNode) { nxt = q; flip = false; break; }
          if (segB[q] == curNode) { nxt = q; flip = true; break; }
        }
      }
      if (nxt < 0) break;
      used[nxt] = true;
      float nx = flip ? segAx[nxt] : segBx[nxt];
      float ny = flip ? segAy[nxt] : segBy[nxt];
      if (!LoopPush(&loop, cx1, cy1)) { loopOK = false; break; }
      cx1 = nx; cy1 = ny;
      curNode = flip ? segA[nxt] : segB[nxt];
      if (curNode == segA[s0]) break; // closed
    }
    // transform to bake space and append
    if (loopOK && loop.n >= 2 && loop.pts) {
      for (A_long i = 0; i < loop.n; i++) {
        double lx = (double)loop.pts[i].x * invS + offX;
        double ly = (double)loop.pts[i].y * invS + offY;
        Pt p = MatApply(m, lx, ly);
        loop.pts[i].x = p.x; loop.pts[i].y = p.y;
      }
      AppendFlatPath(cx, loop.pts, loop.n, true, 0);
    }
    if (loop.pts) free(loop.pts);
  }
  if (nodeA) free(nodeA);
  if (nodeB) free(nodeB);
  free(used); free(segA); free(segB); free(segAx); free(segAy); free(segBx); free(segBy);
}

#define SIL_MAX_DIM 768

/* merged silhouette of a layer's mask stack (Add/Subtract/Intersect/Difference
   + invert, in stack order), appended as closed kind=0 paths in bake space.
   silBBox (layer space, nullable) receives the tight silhouette bounds. */
static void BakeLayerMaskSilhouette(Suites *suites, PF_InData *in_data,
                                    AEGP_LayerH L, const Mat &m, A_Time *t, BakeCtx *cx,
                                    float *silBBox)
{
  if (silBBox) { silBBox[0] = silBBox[1] = 1e30f; silBBox[2] = silBBox[3] = -1e30f; }
  if (!suites->mask || !suites->str) return;
  A_long nM = 0;
  if (suites->mask->AEGP_GetLayerNumMasks(L, &nM) || nM < 1) return;
  DbgLog("sil: nM=%ld\n", (long)nM);

  LoopArr *loops = (LoopArr *)calloc(nM, sizeof(LoopArr));
  if (!loops) return;
  float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
  A_long got = 0;
  for (A_long i = 0; i < nM; i++) {
    AEGP_MaskRefH mH = NULL;
    if (suites->mask->AEGP_GetLayerMaskByIndex(L, i, &mH) || !mH) continue;
    AEGP_StreamRefH sH = NULL;
    if (!suites->str->AEGP_GetNewMaskStream(0, mH, AEGP_MaskStream_OUTLINE, &sH) && sH) {
      AEGP_StreamValue2 val;
      if (!suites->str->AEGP_GetNewStreamValue(0, sH, AEGP_LTimeMode_CompTime, t, FALSE, &val)) {
        LoopArr *lp = &loops[got];
        lp->pts = NULL; lp->n = lp->cap = 0;
        bool closed = true;
        bool loopOK = FlattenMaskOutline(suites, val.val.mask, lp, &closed);
        suites->str->AEGP_DisposeStreamValue(&val);
        PF_MaskMode mm = PF_MaskMode_ADD;
        suites->mask->AEGP_GetMaskMode(mH, &mm);
        A_Boolean inv = FALSE;
        suites->mask->AEGP_GetMaskInvert(mH, &inv);
        lp->mode = (A_long)mm;
        lp->invert = (inv != FALSE);
        if (loopOK && lp->n >= 3) {
          for (A_long q = 0; q < lp->n; q++) {
            if (lp->pts[q].x < bx0) bx0 = lp->pts[q].x;
            if (lp->pts[q].y < by0) by0 = lp->pts[q].y;
            if (lp->pts[q].x > bx1) bx1 = lp->pts[q].x;
            if (lp->pts[q].y > by1) by1 = lp->pts[q].y;
          }
          got++;
        } else {
          if (lp->pts) free(lp->pts);
        }
      }
      suites->str->AEGP_DisposeStream(sH);
    }
    suites->mask->AEGP_DisposeMask(mH);
  }
  DbgLog("sil: got=%ld bounds=%.0f,%.0f-%.0f,%.0f\n", (long)got, bx0, by0, bx1, by1);
  if (got < 1) { free(loops); return; }

  // raster area = union bounds of all loops (padded), long side <= SIL_MAX_DIM
  float pad = 4.0f;
  bx0 -= pad; by0 -= pad; bx1 += pad; by1 += pad;
  double rw = bx1 - bx0, rh = by1 - by0;
  if (rw < 2 || rh < 2) { for (A_long i = 0; i < got; i++) free(loops[i].pts); free(loops); return; }
  double s = (rw > rh ? rw : rh) > SIL_MAX_DIM ? SIL_MAX_DIM / (rw > rh ? rw : rh) : 1.0;
  int bw = (int)(rw * s + 0.5), bh = (int)(rh * s + 0.5);
  unsigned char *acc = (unsigned char *)calloc((size_t)bw * bh, 1);
  unsigned char *cur = (unsigned char *)malloc((size_t)bw * bh);
  if (!acc || !cur) {
    if (acc) free(acc); if (cur) free(cur);
    for (A_long i = 0; i < got; i++) free(loops[i].pts);
    free(loops);
    return;
  }
  bool anyContent = false;
  for (A_long i = 0; i < got; i++) {
    LoopArr *lp = &loops[i];
    LoopArr sc;
    sc.pts = (Pt *)malloc(sizeof(Pt) * lp->n);
    if (!sc.pts) continue;
    sc.n = lp->n; sc.cap = lp->n;
    for (A_long q = 0; q < lp->n; q++) {
      sc.pts[q].x = (float)((lp->pts[q].x - bx0) * s);
      sc.pts[q].y = (float)((lp->pts[q].y - by0) * s);
    }
    memset(cur, 0, (size_t)bw * bh);
    FillLoopXor(cur, bw, bh, &sc);
    free(sc.pts);
    if (lp->invert)
      for (long q = 0; q < (long)bw * bh; q++) cur[q] ^= 1;
    // mode: first mask / ADD fills, SUBTRACT clears, INTERSECT keeps, DIFFERENCE toggles
    if (i == 0 || lp->mode == PF_MaskMode_ADD || lp->mode == PF_MaskMode_NONE) {
      for (long q = 0; q < (long)bw * bh; q++) acc[q] |= cur[q];
      anyContent = true;
    } else if (lp->mode == PF_MaskMode_SUBTRACT) {
      for (long q = 0; q < (long)bw * bh; q++) acc[q] &= (unsigned char)(cur[q] ^ 1);
    } else if (lp->mode == PF_MaskMode_INTERSECT) {
      for (long q = 0; q < (long)bw * bh; q++) acc[q] &= cur[q];
    } else if (lp->mode == PF_MaskMode_DIFFERENCE) {
      for (long q = 0; q < (long)bw * bh; q++) acc[q] ^= cur[q];
    } else {
      for (long q = 0; q < (long)bw * bh; q++) acc[q] |= cur[q];
    }
  }
  for (A_long i = 0; i < got; i++) free(loops[i].pts);
  free(loops);
  free(cur);
  if (!anyContent) { free(acc); return; }

  if (silBBox) {
    // tight bounds of the filled area, in layer space
    int sx0 = bw, sy0 = bh, sx1 = -1, sy1 = -1;
    for (int y = 0; y < bh; y++)
      for (int x = 0; x < bw; x++)
        if (acc[y * bw + x]) {
          if (x < sx0) sx0 = x; if (x > sx1) sx1 = x;
          if (y < sy0) sy0 = y; if (y > sy1) sy1 = y;
        }
    if (sx1 >= 0) {
      silBBox[0] = (float)(bx0 + sx0 / s); silBBox[1] = (float)(by0 + sy0 / s);
      silBBox[2] = (float)(bx0 + (sx1 + 1) / s); silBBox[3] = (float)(by0 + (sy1 + 1) / s);
    }
  }
  A_long pcBefore = ((GeoHeader *)cx->blob->base)->pathCount;
  TraceBitmapSilhouette(acc, bw, bh, (float)(1.0 / s), bx0, by0, m, cx);
  DbgLog("sil: traced paths=%ld (bw=%d bh=%d)\n",
         (long)(((GeoHeader *)cx->blob->base)->pathCount - pcBefore), bw, bh);
  free(acc);
}

/* motion polyline of an arbitrary root 2D layer, sampled over the comp in comp
   space, stored as an open kind=1 path (drawn in the motion color) */
static void BakeLayerMotionBelow(Suites *suites, PF_InData *in_data,
                                 AEGP_LayerH L, A_Time *t, A_long occIndex, BakeCtx *cx)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  Blob *b = cx->blob;
  if (!suites->str || !suites->lay) return;
  AEGP_LayerH parentH = NULL;
  if (!suites->lay->AEGP_GetLayerParent(L, &parentH) && parentH) return;
  if (gh->pathCount >= GEO_MAX_PATHS) return;

  AEGP_StreamType posType = AEGP_StreamType_NO_DATA;
  AEGP_StreamVal2 v0;
  if (suites->str->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_POSITION,
                                            AEGP_LTimeMode_CompTime, t, FALSE, &v0, &posType))
    return;
  if (posType != AEGP_StreamType_TwoD && posType != AEGP_StreamType_TwoD_SPATIAL &&
      posType != AEGP_StreamType_ThreeD && posType != AEGP_StreamType_ThreeD_SPATIAL)
    return;

  double dur = NowSec(in_data, in_data->total_time);
  A_long off = (b->len - cx->flatStart) / (A_long)sizeof(Pt);
  A_long n = 0;
  double maxDev = 0, x0 = 0, y0 = 0;
  for (A_long i = 0; i <= MOTION_SAMPLES; i++) {
    double sec = dur * (double)i / MOTION_SAMPLES;
    A_Time ts; TimeOf(in_data, sec, &ts);
    AEGP_StreamVal2 val;
    if (suites->str->AEGP_GetLayerStreamValue(L, AEGP_LayerStream_POSITION,
                                              AEGP_LTimeMode_CompTime, &ts, FALSE, &val, NULL))
      break;
    double px, py;
    if (posType == AEGP_StreamType_ThreeD || posType == AEGP_StreamType_ThreeD_SPATIAL) {
      px = val.three_d.x; py = val.three_d.y;
    } else { px = val.two_d.x; py = val.two_d.y; }
    if (i == 0) { x0 = px; y0 = py; }
    double dev = fabs(px - x0) + fabs(py - y0);
    if (dev > maxDev) maxDev = dev;
    Pt *pt = (Pt *)BlobAt(b, sizeof(Pt));
    if (!pt) return;
    pt->x = (float)px; pt->y = (float)py;
    n++;
  }
  if (n < 2 || maxDev < 0.5) {
    b->len = cx->flatStart + off * (A_long)sizeof(Pt); // static layer: drop the polyline
    return;
  }
  PathDesc *pd = (PathDesc *)(b->base + cx->pdBase + gh->pathCount * sizeof(PathDesc));
  gh->pathCount++;
  pd->off = off; pd->n = n; pd->closed = 0; pd->kind = 1; pd->occ = occIndex;
}

/* projected comp-space quad of a 3D layer (four layer corners pushed through
   the world matrix and the active camera) — frame + outer edge for 3D layers */
static void Bake3DLayerQuad(Suites *suites, PF_InData *in_data,
                            AEGP_LayerH L, A_Time *t, BakeCtx *cx)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  if (!suites->lay || !suites->pfi || !suites->item) return;
  AEGP_ItemH itemH = NULL;
  if (suites->lay->AEGP_GetLayerSourceItem(L, &itemH) || !itemH) return;
  A_long w = 0, h = 0;
  if (suites->item->AEGP_GetItemDimensions(itemH, &w, &h) || w < 1 || h < 1) return;

  A_Matrix4 M;
  if (suites->lay->AEGP_GetLayerToWorldXform(L, t, &M)) return;
  DbgLog("3d: M |%.0f %.0f %.0f %.0f| %.0f %.0f %.0f %.0f| %.0f %.0f %.0f %.0f| %.0f %.0f %.1f %.1f|\n",
         M.mat[0][0], M.mat[0][1], M.mat[0][2], M.mat[0][3],
         M.mat[1][0], M.mat[1][1], M.mat[1][2], M.mat[1][3],
         M.mat[2][0], M.mat[2][1], M.mat[2][2], M.mat[2][3],
         M.mat[3][0], M.mat[3][1], M.mat[3][2], M.mat[3][3]);
  A_Matrix4 C;
  A_FpLong dist = 0;
  A_short iw = 0, ih = 0;
  A_Err ce = suites->pfi->AEGP_GetEffectCameraMatrix(in_data->effect_ref, t, &C, &dist, &iw, &ih);
  DbgLog("3d: camera err=%d dist=%.1f iw=%d ih=%d\n", (int)ce, (double)dist, (int)iw, (int)ih);
  if (ce) return;
  if (dist < 1e-6 || iw < 2 || ih < 2) return;

  Pt quad[4];
  const double cx4[4] = {0, (double)w, (double)w, 0};
  const double cy4[4] = {0, 0, (double)h, (double)h};
  for (int i = 0; i < 4; i++) {
    // layer space -> world (A_Matrix4: row-vector convention, translation in row 3)
    double wx = cx4[i] * M.mat[0][0] + cy4[i] * M.mat[1][0] + M.mat[3][0];
    double wy = cx4[i] * M.mat[0][1] + cy4[i] * M.mat[1][1] + M.mat[3][1];
    double wz = cx4[i] * M.mat[0][2] + cy4[i] * M.mat[1][2] + M.mat[3][2];
    // world -> camera view. AEGP_GetEffectCameraMatrix returns the camera's
    // WORLD placement (v' = v*C); the view matrix is its rigid inverse
    // (R^T, -R^T t) under the row-vector convention.
    double dx = wx - C.mat[3][0], dy = wy - C.mat[3][1], dz = wz - C.mat[3][2];
    double vx = dx * C.mat[0][0] + dy * C.mat[0][1] + dz * C.mat[0][2];
    double vy = dx * C.mat[1][0] + dy * C.mat[1][1] + dz * C.mat[1][2];
    double vz = dx * C.mat[2][0] + dy * C.mat[2][1] + dz * C.mat[2][2];
    if (i == 0)
      DbgLog("3d: c0 w=(%.0f,%.0f,%.0f) v=(%.0f,%.0f,%.0f)\n", wx, wy, wz, vx, vy, vz);
    if (fabs(vz) < 1e-6) return;
    // project onto the image plane (in front of the camera: vz > 0)
    double pz = vz;
    if (pz < 1e-6) pz = 1e-6;
    quad[i].x = (float)(iw / 2.0 + vx * dist / pz);
    quad[i].y = (float)(ih / 2.0 + vy * dist / pz);
  }
  DbgLog("3d: quad (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f) (%.0f,%.0f)\n",
         quad[0].x, quad[0].y, quad[1].x, quad[1].y, quad[2].x, quad[2].y, quad[3].x, quad[3].y);
  // drill-down keeps the projected quad outline; the FRAME is the dynamic
  // axis-aligned bbox of it (visually distinct, tracks 3D rotation). The same
  // projected quad is used only as the moving layer's motion-path occluder.
  AppendFlatPath(cx, quad, 4, true, 0);
  AppendOccluderQuad(cx, quad);
  if (gh->frameN < GEO_MAX_FRAMES) {
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    for (int i = 0; i < 4; i++) {
      if (quad[i].x < bx0) bx0 = quad[i].x;
      if (quad[i].y < by0) by0 = quad[i].y;
      if (quad[i].x > bx1) bx1 = quad[i].x;
      if (quad[i].y > by1) by1 = quad[i].y;
    }
    Pt *fq = (Pt *)(cx->blob->base + cx->frameBase + gh->frameN * 4 * sizeof(Pt));
    fq[0].x = bx0; fq[0].y = by0; fq[1].x = bx1; fq[1].y = by0;
    fq[2].x = bx1; fq[2].y = by1; fq[3].x = bx0; fq[3].y = by1;
    gh->frameN++;
  }
}

/* Build an occluder for one 2D layer. Prefer the actual baked geometry
   bounds (masks/text/shapes); for ordinary solids/footage fall back to the
   transformed source rectangle. This is associated with that layer's motion
   path only, so a background layer never hides every other layer's trail. */
static A_long AppendLayerOccluder(Suites *suites, AEGP_LayerH L, const Mat &lm,
                                  A_long pathBefore, A_long pathEnd, BakeCtx *cx)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  const Pt *flat = (const Pt *)(cx->blob->base + cx->flatStart);
  Pt q[4];
  bool haveBounds = false;
  if (pathEnd > pathBefore) {
    const PathDesc *paths = (const PathDesc *)(cx->blob->base + cx->pdBase);
    float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
    for (A_long pi = pathBefore; pi < pathEnd; pi++) {
      if (paths[pi].kind != 0) continue;
      A_long p0 = paths[pi].off, p1 = paths[pi].off + paths[pi].n;
      if (p0 < 0) p0 = 0;
      if (p1 > (A_long)((cx->blob->len - cx->flatStart) / (A_long)sizeof(Pt)))
        p1 = (A_long)((cx->blob->len - cx->flatStart) / (A_long)sizeof(Pt));
      for (A_long i = p0; i < p1; i++) {
        if (!isfinite(flat[i].x) || !isfinite(flat[i].y)) continue;
        if (flat[i].x < bx0) bx0 = flat[i].x;
        if (flat[i].y < by0) by0 = flat[i].y;
        if (flat[i].x > bx1) bx1 = flat[i].x;
        if (flat[i].y > by1) by1 = flat[i].y;
      }
    }
    if (bx1 >= bx0 && by1 >= by0) {
      q[0].x = bx0; q[0].y = by0;
      q[1].x = bx1; q[1].y = by0;
      q[2].x = bx1; q[2].y = by1;
      q[3].x = bx0; q[3].y = by1;
      haveBounds = true;
    }
  }
  if (!haveBounds && suites->lay && suites->item) {
    AEGP_ItemH itemH = NULL;
    A_long w = 0, h = 0;
    if (!suites->lay->AEGP_GetLayerSourceItem(L, &itemH) && itemH &&
        !suites->item->AEGP_GetItemDimensions(itemH, &w, &h) && w > 0 && h > 0) {
      q[0] = MatApply(lm, 0, 0);
      q[1] = MatApply(lm, (double)w, 0);
      q[2] = MatApply(lm, (double)w, (double)h);
      q[3] = MatApply(lm, 0, (double)h);
      haveBounds = true;
    }
  }
  if (!haveBounds) return -1;
  A_long idx = AppendOccluderQuad(cx, q);
  if (idx < 0) DbgLog("motion occluder reserve full (paths=%ld)\n", (long)gh->pathCount);
  return idx;
}

/* bake every drawable layer below selfH (adjustment-layer mode), all in comp space */
static void BakeLayersBelow(Suites *suites, PF_InData *in_data,
                            AEGP_LayerH selfH, BakeCtx *cx)
{
  GeoHeader *gh = (GeoHeader *)cx->blob->base;
  if (!suites->lay) { gh->status |= 2; return; }
  AEGP_CompH compH = NULL;
  if (suites->lay->AEGP_GetLayerParentComp(selfH, &compH) || !compH) { gh->status |= 2; return; }
  A_long myIdx = 0, nL = 0;
  if (suites->lay->AEGP_GetLayerIndex(selfH, &myIdx)) { gh->status |= 2; return; }
  if (suites->lay->AEGP_GetCompNumLayers(compH, &nL)) { gh->status |= 2; return; }
  A_Time t; t.value = in_data->current_time; t.scale = in_data->time_scale;

  A_long fromIdx = myIdx + 1;
  if (getenv("EVIZ_ALL_LAYERS")) fromIdx = 0; // test hook: bake the whole stack
  for (A_long idx = fromIdx; idx < nL; idx++) {
    if (idx == myIdx) continue;
    AEGP_LayerH L = NULL;
    if (suites->lay->AEGP_GetCompLayerByIndex(compH, idx, &L) || !L) continue;
    AEGP_LayerFlags fl = AEGP_LayerFlag_NONE;
    if (suites->lay->AEGP_GetLayerFlags(L, &fl)) continue;
    if (!(fl & AEGP_LayerFlag_VIDEO_ACTIVE)) continue;
    if (fl & (AEGP_LayerFlag_ADJUSTMENT_LAYER | AEGP_LayerFlag_GUIDE_LAYER |
              AEGP_LayerFlag_NULL_LAYER)) continue;
    AEGP_ObjectType ot = AEGP_ObjectType_AV;
    if (suites->lay->AEGP_GetLayerObjectType(L, &ot)) continue;
    if (ot == AEGP_ObjectType_LIGHT || ot == AEGP_ObjectType_CAMERA) continue;
    DbgLog("below: idx=%ld ot=%ld 3d=%d\n", (long)idx, (long)ot,
           (int)((fl & AEGP_LayerFlag_LAYER_IS_3D) != 0));
    if (fl & AEGP_LayerFlag_LAYER_IS_3D) {
      Bake3DLayerQuad(suites, in_data, L, &t, cx);
      continue;
    }

    LayerX lx;
    ReadLayerXform(suites, L, &t, &lx);
    Mat lm = MatFromLayerX(&lx);

    A_long flatBefore = (cx->blob->len - cx->flatStart) / (A_long)sizeof(Pt);
    A_long pathBefore = gh->pathCount;
    AEGP_ItemH sourceItem = NULL;
    AEGP_ItemType sourceType = AEGP_ItemType_NONE;
    if (!suites->lay->AEGP_GetLayerSourceItem(L, &sourceItem) && sourceItem)
      suites->item->AEGP_GetItemType(sourceItem, &sourceType);
    if (sourceType == AEGP_ItemType_COMP) {
      BakeNestedCompContents(suites, in_data, L, sourceItem, &t, lm, cx, 0);
    }
    float silBB[4];
    BakeLayerMaskSilhouette(suites, in_data, L, lm, &t, cx, silBB);
    A_Time childLayerT = LayerTimeAtComp(suites, L, &t);
    BakeTextOutlines(suites, in_data, L, &childLayerT, cx); // glyph coords are comp space
    BakeShapes(suites, in_data, L, &childLayerT, lm, cx);
    // geometry ends here; the motion polyline does not join the geometry bounds
    // directly — the frame grows with the TRAVELLED part of the path instead.
    // Keep a separate current-content occluder so the trail is under this layer
    // but remains visible over unrelated/background layers.
    A_long pathGeoEnd = gh->pathCount;
    A_long occIndex = cx->needMotion
                        ? AppendLayerOccluder(suites, L, lm, pathBefore, pathGeoEnd, cx)
                        : -1;
    // Shape-item motion paths were baked before the layer motion path. They
    // need the same current-layer occluder; otherwise a trail can be painted
    // across the moving shape itself.
    for (A_long pi = pathBefore; pi < pathGeoEnd; pi++)
      if (((PathDesc *)(cx->blob->base + cx->pdBase))[pi].kind == 1 &&
          ((PathDesc *)(cx->blob->base + cx->pdBase))[pi].occ < 0)
        ((PathDesc *)(cx->blob->base + cx->pdBase))[pi].occ = occIndex;
    if (cx->needMotion)
      BakeLayerMotionBelow(suites, in_data, L, &t, occIndex, cx);

    // Frame is based on current structure plus travelled motion prefixes.
    // Never include the complete future motion path in the frame at t=0.
    bool wantFrame = (ot != AEGP_ObjectType_TEXT) || cx->showTFrame;
    if (wantFrame && gh->frameN < GEO_MAX_FRAMES) {
      const Pt *flat = (const Pt *)(cx->blob->base + cx->flatStart);
      const PathDesc *allPaths = (const PathDesc *)(cx->blob->base + cx->pdBase);
      A_long currentFlatTotal = (cx->blob->len - cx->flatStart) / (A_long)sizeof(Pt);
      float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
      bool have = false;
      for (A_long pi = pathBefore; pi < gh->pathCount; pi++) {
        const PathDesc *pd = &allPaths[pi];
        if (pd->kind == 1 && ot == AEGP_ObjectType_TEXT) continue;
        A_long cut = pd->kind == 1 ? MotionPrefixCount(in_data, gh->t0, gh->t1, pd->n) : pd->n;
        for (A_long fi = pd->off; fi < pd->off + cut && fi < currentFlatTotal; fi++) {
          if (flat[fi].x < bx0) bx0 = flat[fi].x;
          if (flat[fi].y < by0) by0 = flat[fi].y;
          if (flat[fi].x > bx1) bx1 = flat[fi].x;
          if (flat[fi].y > by1) by1 = flat[fi].y;
          have = true;
        }
      }
      // Always include the moving object's current content bounds. The motion
      // path is stored as a centerline, so without this quad a solid/shape
      // frame can collapse to the centerline at the first frame.
      if (occIndex >= 0 && occIndex < gh->occCount) {
        const OccQuad *oq = (const OccQuad *)(cx->blob->base + cx->occBase) + occIndex;
        for (int qi = 0; qi < 4; qi++) {
          if (oq->p[qi].x < bx0) bx0 = oq->p[qi].x; if (oq->p[qi].y < by0) by0 = oq->p[qi].y;
          if (oq->p[qi].x > bx1) bx1 = oq->p[qi].x; if (oq->p[qi].y > by1) by1 = oq->p[qi].y;
        }
        have = true;
      }
      if (!have && ot != AEGP_ObjectType_TEXT && suites->item) {
        AEGP_ItemH itemH = NULL;
        if (!suites->lay->AEGP_GetLayerSourceItem(L, &itemH) && itemH) {
          A_long w = 0, h = 0;
          if (!suites->item->AEGP_GetItemDimensions(itemH, &w, &h) && w > 0 && h > 0) {
            Pt q[4] = {MatApply(lm, 0, 0), MatApply(lm, (double)w, 0),
                       MatApply(lm, (double)w, (double)h), MatApply(lm, 0, (double)h)};
            for (int qi = 0; qi < 4; qi++) {
              if (q[qi].x < bx0) bx0 = q[qi].x; if (q[qi].y < by0) by0 = q[qi].y;
              if (q[qi].x > bx1) bx1 = q[qi].x; if (q[qi].y > by1) by1 = q[qi].y;
            }
            have = true;
          }
        }
      }
      if (have) {
        Pt *fq = (Pt *)(cx->blob->base + cx->frameBase + gh->frameN * 4 * sizeof(Pt));
        fq[0].x = bx0; fq[0].y = by0; fq[1].x = bx1; fq[1].y = by0;
        fq[2].x = bx1; fq[2].y = by1; fq[3].x = bx0; fq[3].y = by1;
        gh->frameN++;
      }
    }
    if (cx->blob->err) return;
  }
}

static PF_Err BakeGeometry(PF_InData *in_data, Blob *blob, A_long thinN, A_long targetMode,
                           bool showTFrame, bool showHandles, bool needKeys, bool needMotion)
{
  PF_Err err = PF_Err_NONE;
  GeoHeader *gh = (GeoHeader *)blob->base;
  Suites suitesStore; Suites *suites = &suitesStore;
  AcquireSuites(in_data, &suitesStore);

  AEGP_LayerH layerH = NULL;
  if (!suites->pfi || suites->pfi->AEGP_GetEffectLayer(in_data->effect_ref, &layerH) || !layerH) {
    gh->status |= 2;
    ReleaseSuites(in_data, &suitesStore);
    return err;
  }

  // resolve target mode (Auto: adjustment layer -> layers below).
  // "Layers Below" draws in comp space, so it is only meaningful on layers
  // whose effect buffer is comp-sized/comp-space (adjustment layers, text,
  // shape); on plain AV solids fall back to this-layer mode.
  bool below = (targetMode == 3);
  if (suites->lay) {
    AEGP_LayerFlags fl = AEGP_LayerFlag_NONE;
    suites->lay->AEGP_GetLayerFlags(layerH, &fl);
    AEGP_ObjectType selfType = AEGP_ObjectType_AV;
    suites->lay->AEGP_GetLayerObjectType(layerH, &selfType);
    bool compSpaceLayer = (fl & AEGP_LayerFlag_ADJUSTMENT_LAYER) != 0 ||
                          selfType == AEGP_ObjectType_TEXT ||
                          selfType == AEGP_ObjectType_VECTOR;
    if (targetMode == 1)
      below = (fl & AEGP_LayerFlag_ADJUSTMENT_LAYER) != 0;
    if (!compSpaceLayer)
      below = false;
  }

  BakeCtx cx;
  cx.in_data = in_data;
  cx.blob = blob;
  cx.thinN = thinN;
  cx.showTFrame = showTFrame;
  cx.showHandles = showHandles;
  cx.needKeys = needKeys;
  cx.needMotion = needMotion;

  gh->trackN = 0; gh->motionN = 0; gh->pathCount = 0; gh->flatTotal = 0;
  gh->keyCount = 0; gh->frameN = 0; gh->occCount = 0;
  blob->len = sizeof(GeoHeader);

  if (!below && needMotion)
    err = BakeMotionAndTrack(suites, in_data, layerH, blob);
  if (blob->err) return blob->err;

  cx.pdBase = blob->len;
  blob->len += GEO_MAX_PATHS * sizeof(PathDesc);
  cx.keyBase = blob->len;
  blob->len += GEO_MAX_KEYS * sizeof(KeyVert);
  cx.frameBase = blob->len;
  blob->len += GEO_MAX_FRAMES * 4 * sizeof(Pt);
  cx.occBase = blob->len;
  blob->len += GEO_MAX_OCC * sizeof(OccQuad);
  cx.flatStart = blob->len;
  gh->frameBase = cx.frameBase;
  gh->occBase = cx.occBase;
  gh->keyBase = cx.keyBase;
  gh->flatBase = cx.flatStart;

  if (below) {
    gh->status |= 4 | 8; // comp-space buffer + layers-below mode
    gh->t0 = 0; gh->t1 = NowSec(in_data, in_data->total_time); // motion prefix range
    BakeLayersBelow(suites, in_data, layerH, &cx);
  } else {
    // own-layer masks -> merged silhouette; solids draw in layer space,
    // vector layers (text/shape) in the comp-space buffer via layer transform
    AEGP_ObjectType selfOT = AEGP_ObjectType_AV;
    if (suites->lay) suites->lay->AEGP_GetLayerObjectType(layerH, &selfOT);
    bool compSpace = (selfOT == AEGP_ObjectType_TEXT || selfOT == AEGP_ObjectType_VECTOR);
    A_Time t; t.value = in_data->current_time; t.scale = in_data->time_scale;
    Mat mm = MatId();
    if (compSpace) {
      LayerX lx;
      ReadLayerXform(suites, layerH, &t, &lx);
      mm = MatFromLayerX(&lx);
    }
    AEGP_ItemH ownSourceItem = NULL;
    AEGP_ItemType ownSourceType = AEGP_ItemType_NONE;
    if (suites->lay && suites->item &&
        !suites->lay->AEGP_GetLayerSourceItem(layerH, &ownSourceItem) && ownSourceItem)
      suites->item->AEGP_GetItemType(ownSourceItem, &ownSourceType);
    if (ownSourceType == AEGP_ItemType_COMP)
      BakeNestedCompContents(suites, in_data, layerH, ownSourceItem, &t, mm, &cx, 0);

    BakeLayerMaskSilhouette(suites, in_data, layerH, mm, &t, &cx, NULL);
    if (blob->err) return blob->err;

    A_Time ownLayerT = LayerTimeAtComp(suites, layerH, &t);
    err = BakeTextOutlines(suites, in_data, layerH, &ownLayerT, &cx);
    if (blob->err) return blob->err;

    err = BakeShapes(suites, in_data, layerH, &ownLayerT, mm, &cx);
    if (blob->err) return blob->err;
  }

  gh->flatTotal = (A_long)((blob->len - cx.flatStart) / sizeof(Pt));
  gh->totalBytes = blob->len;
  ReleaseSuites(in_data, &suitesStore);
  return err;
}
static PF_Err
GlobalSetup(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  PF_Err err = PF_Err_NONE;
  out_data->my_version = PF_VERSION(MAJOR_VERSION, MINOR_VERSION, BUG_VERSION, STAGE_VERSION, BUILD_VERSION);
  out_data->out_flags |= PF_OutFlag_PIX_INDEPENDENT | PF_OutFlag_USE_OUTPUT_EXTENT |
                         PF_OutFlag_SEND_UPDATE_PARAMS_UI | PF_OutFlag_NON_PARAM_VARY |
                         PF_OutFlag_WIDE_TIME_INPUT;
  out_data->out_flags2 |= PF_OutFlag2_I_USE_3D_CAMERA |
                          PF_OutFlag2_PARAM_GROUP_START_COLLAPSED_FLAG;
  return err;
}

/* ================= debug log (env EVIZ_DEBUG or /tmp/ev_dbg_on gated) ================= */

#include <stdarg.h>
#include <unistd.h>

static bool DbgOn()
{
  return getenv("EVIZ_DEBUG") != NULL || access("/tmp/ev_dbg_on", F_OK) == 0;
}
static void DbgLog(const char *fmt, ...)
{
  if (!DbgOn()) return;
  FILE *f = fopen("/tmp/ev_dbg.log", "a");
  if (!f) return;
  va_list ap;
  va_start(ap, fmt);
  vfprintf(f, fmt, ap);
  va_end(ap);
  fclose(f);
}

static bool NeedGeoParams(PF_ParamDef *params[])
{
  return (params[PARAM_SHOW_FRAME]->u.bd.value   != 0) ||
         (params[PARAM_SHOW_MASK]->u.bd.value    != 0) ||
         (params[PARAM_SHOW_VERTS]->u.bd.value   != 0) ||
         (params[PARAM_SHOW_HANDLES]->u.bd.value != 0) ||
         (params[PARAM_SHOW_TEXT]->u.bd.value    != 0) ||
         (params[PARAM_SHOW_SHAPE]->u.bd.value   != 0) ||
         (params[PARAM_SHOW_TFRAME]->u.bd.value  != 0) ||
         (params[PARAM_SHOW_MOTION]->u.bd.value  != 0) ||
         (params[PARAM_TARGET]->u.pd.value       == 3);
}

static A_long TargetParam(PF_ParamDef *params[])
{
  A_long target = (A_long)params[PARAM_TARGET]->u.pd.value;
  // test override: EVIZ_FORCE_TARGET=2|3 forces the target mode (aerender
  // cannot set params; harmless when unset)
  const char *ft = getenv("EVIZ_FORCE_TARGET");
  if (ft && (ft[0] == '2' || ft[0] == '3') && ft[1] == 0)
    target = ft[0] - '0';
  return target;
}

static bool IsAerenderProc()
{
  static const bool isAerender = [] {
    const char *pn = getprogname();
    return pn && strstr(pn, "aerender") != NULL;
  }();
  return isAerender;
}

static Blob *BakeBlob(PF_InData *in_data, PF_ParamDef *params[])
{
  Blob *blob = BlobNew(GEO_BLOB_CAP);
  if (!blob) return NULL;
  memset(blob->base, 0, sizeof(GeoHeader));
  ((GeoHeader *)blob->base)->magic = GEO_MAGIC;
  const bool showFrame = params[PARAM_SHOW_FRAME]->u.bd.value != 0;
  const bool showMotion = params[PARAM_SHOW_MOTION]->u.bd.value != 0;
  const bool needKeys = params[PARAM_SHOW_VERTS]->u.bd.value != 0 ||
                        params[PARAM_SHOW_HANDLES]->u.bd.value != 0;
  BakeGeometry(in_data, blob,
               (A_long)params[PARAM_THIN_N]->u.sd.value,
               TargetParam(params),
               params[PARAM_SHOW_TFRAME]->u.bd.value != 0,
               params[PARAM_SHOW_HANDLES]->u.bd.value != 0,
               needKeys,
               showFrame || showMotion);
  return blob;
}

/* FRAME_SETUP is sent on the UI thread immediately before each frame render;
   all AEGP geometry queries happen here and are cached in frame_data.
   (AEGP calls during PF_Cmd_RENDER trip AE's render-thread validation —
   the "internal verification failure" seen in the GUI.) */
static PF_Err
FrameSetup8(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  // AEGP is only legal on the main (UI) thread; AE validates this and wedged
  // the GUI with "internal verification" errors when we baked during
  // PF_Cmd_RENDER. aerender runs selectors off the main thread but does not
  // enforce that validation (the old render-time bake always worked there),
  // so: aerender process -> always bake; GUI process -> bake only when this
  // selector arrived on the main thread. On a skip, Render8 bakes inline.
  const bool mainT = pthread_main_np() != 0;
  const bool skip  = !IsAerenderProc() && !mainT;
  DbgLog("FS8: main=%d aer=%d needGeo=%d target=%ld skip=%d\n",
         (int)mainT, (int)IsAerenderProc(), (int)NeedGeoParams(params),
         (long)TargetParam(params), (int)skip);
  if (skip) {
    out_data->frame_data = NULL;
    return PF_Err_NONE;
  }
  Blob *blob = NULL;
  if (NeedGeoParams(params)) {
    blob = BakeBlob(in_data, params);
    if (blob) {
      GeoHeader *gh = (GeoHeader *)blob->base;
      DbgLog("FS8: baked paths=%ld keys=%ld frames=%ld status=%ld\n",
             (long)gh->pathCount, (long)gh->keyCount, (long)gh->frameN, (long)gh->status);
    }
  }
  out_data->frame_data = (PF_Handle)blob;
  return PF_Err_NONE;
}

static PF_Err
FrameSetdown8(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  if (in_data->frame_data) {
    BlobFree((Blob *)in_data->frame_data);
    out_data->frame_data = NULL;
  }
  return PF_Err_NONE;
}

/* ================= drawing (8bpc straight alpha) ================= */

static inline PF_Pixel8 *
OutRow(const PF_LayerDef *output, A_long y)
{
  return (PF_Pixel8 *)((char *)output->data + (size_t)y * (size_t)output->rowbytes);
}

static inline void CompositeCover(PF_Pixel8 *px, const PF_Pixel8 *col, float cover)
{
  if (cover <= 0.0f) return;
  if (cover > 1.0f) cover = 1.0f;
  const float ca = cover;
  const float oa = (float)px->alpha / 255.0f;
  const float outA = ca + oa * (1.0f - ca);
  if (outA > 0.0001f) {
    px->red   = (A_u_char)((col->red   * ca + px->red   * oa * (1.0f - ca)) / outA + 0.5f);
    px->green = (A_u_char)((col->green * ca + px->green * oa * (1.0f - ca)) / outA + 0.5f);
    px->blue  = (A_u_char)((col->blue  * ca + px->blue  * oa * (1.0f - ca)) / outA + 0.5f);
  }
  px->alpha = (A_u_char)(outA * 255.0f + 0.5f);
}

static void DrawDot(PF_LayerDef *output, const PF_Rect *clip,
                    float cx, float cy, float radius, const PF_Pixel8 *col)
{
  int x0 = (int)floorf(cx - radius - 1), x1 = (int)ceilf(cx + radius + 1);
  int y0 = (int)floorf(cy - radius - 1), y1 = (int)ceilf(cy + radius + 1);
  if (x0 < clip->left) x0 = clip->left;
  if (y0 < clip->top) y0 = clip->top;
  if (x1 > clip->right) x1 = clip->right;
  if (y1 > clip->bottom) y1 = clip->bottom;
  for (int y = y0; y < y1; y++) {
    PF_Pixel8 *row = OutRow(output, y);
    for (int x = x0; x < x1; x++) {
      float dx = (float)x + 0.5f - cx, dy = (float)y + 0.5f - cy;
      float d = sqrtf(dx * dx + dy * dy);
      CompositeCover(&row[x], col, radius + 0.5f - d);
    }
  }
}

static void DrawLine(PF_LayerDef *output, const PF_Rect *clip,
                     float x0, float y0, float x1, float y1,
                     float width, const PF_Pixel8 *col)
{
  float minX = x0 < x1 ? x0 : x1, maxX = x0 > x1 ? x0 : x1;
  float minY = y0 < y1 ? y0 : y1, maxY = y0 > y1 ? y0 : y1;
  float pad = width * 0.5f + 1.0f;
  int ix0 = (int)floorf(minX - pad), ix1 = (int)ceilf(maxX + pad);
  int iy0 = (int)floorf(minY - pad), iy1 = (int)ceilf(maxY + pad);
  if (ix0 < clip->left) ix0 = clip->left;
  if (iy0 < clip->top) iy0 = clip->top;
  if (ix1 > clip->right) ix1 = clip->right;
  if (iy1 > clip->bottom) iy1 = clip->bottom;
  float vx = x1 - x0, vy = y1 - y0;
  float len2 = vx * vx + vy * vy;
  for (int y = iy0; y < iy1; y++) {
    PF_Pixel8 *row = OutRow(output, y);
    for (int x = ix0; x < ix1; x++) {
      float px_ = (float)x + 0.5f, py_ = (float)y + 0.5f;
      float t = 0.0f;
      if (len2 > 0.0001f) {
        t = ((px_ - x0) * vx + (py_ - y0) * vy) / len2;
        if (t < 0) t = 0; if (t > 1) t = 1;
      }
      float dx = px_ - (x0 + t * vx), dy = py_ - (y0 + t * vy);
      float d = sqrtf(dx * dx + dy * dy);
      CompositeCover(&row[x], col, width * 0.5f + 0.5f - d);
    }
  }
}

static void DrawPolyline(PF_LayerDef *output, const PF_Rect *clip,
                         const Pt *pts, A_long n, bool closed,
                         float width, const PF_Pixel8 *col)
{
  if (n < 2) return;
  A_long spans = closed ? n : n - 1;
  for (A_long i = 0; i < spans; i++) {
    A_long j = closed ? (i + 1) % n : i + 1;
    DrawLine(output, clip, pts[i].x, pts[i].y, pts[j].x, pts[j].y, width, col);
  }
}

static void DrawSquare(PF_LayerDef *output, const PF_Rect *clip,
                       float cx, float cy, float half, const PF_Pixel8 *col)
{
  DrawLine(output, clip, cx - half, cy - half, cx + half, cy - half, 1.2f, col);
  DrawLine(output, clip, cx + half, cy - half, cx + half, cy + half, 1.2f, col);
  DrawLine(output, clip, cx + half, cy + half, cx - half, cy + half, 1.2f, col);
  DrawLine(output, clip, cx - half, cy + half, cx - half, cy - half, 1.2f, col);
  DrawDot(output, clip, cx, cy, half * 0.7f, col);
}

static inline float Cross2(float ax, float ay, float bx, float by)
{ return ax * by - ay * bx; }

static bool PointInQuad(const OccQuad *q, float x, float y)
{
  bool pos = false, neg = false;
  for (int i = 0; i < 4; i++) {
    const Pt &a = q->p[i], &b = q->p[(i + 1) & 3];
    float c = Cross2(b.x - a.x, b.y - a.y, x - a.x, y - a.y);
    if (c > 0.001f) pos = true;
    if (c < -0.001f) neg = true;
  }
  return !(pos && neg);
}

static void PushT(float *ts, int *n, float t)
{
  if (t < -0.0001f || t > 1.0001f || *n >= 640) return;
  if (t < 0) t = 0; if (t > 1) t = 1;
  for (int i = 0; i < *n; i++) if (fabsf(ts[i] - t) < 0.0001f) return;
  ts[(*n)++] = t;
}

static void SegmentEdgeT(float x0, float y0, float x1, float y1,
                         const Pt &a, const Pt &b, float *ts, int *n)
{
  float rx = x1 - x0, ry = y1 - y0;
  float sx = b.x - a.x, sy = b.y - a.y;
  float den = Cross2(rx, ry, sx, sy);
  float qpx = a.x - x0, qpy = a.y - y0;
  if (fabsf(den) < 1e-6f) return;
  float t = Cross2(qpx, qpy, sx, sy) / den;
  float u = Cross2(qpx, qpy, rx, ry) / den;
  if (t >= -0.0001f && t <= 1.0001f && u >= -0.0001f && u <= 1.0001f)
    PushT(ts, n, t);
}

/* Draw a motion segment only in the portions not covered by its associated
   layer quad. Segment subdivision happens at every quad-edge intersection, so
   the line is genuinely behind the solid instead of merely being drawn first
   over the already-composited source image. */
static void DrawLineBehind(PF_LayerDef *output, const PF_Rect *clip,
                           float x0, float y0, float x1, float y1, float width,
                           const PF_Pixel8 *col, const OccQuad *occs, A_long occCount)
{
  if (!occs || occCount <= 0) {
    DrawLine(output, clip, x0, y0, x1, y1, width, col);
    return;
  }
  float ts[640]; int nt = 0;
  PushT(ts, &nt, 0.0f); PushT(ts, &nt, 1.0f);
  for (A_long oi = 0; oi < occCount; oi++)
    for (int e = 0; e < 4; e++)
      SegmentEdgeT(x0, y0, x1, y1, occs[oi].p[e], occs[oi].p[(e + 1) & 3], ts, &nt);
  for (int i = 1; i < nt; i++) {
    float v = ts[i]; int j = i - 1;
    while (j >= 0 && ts[j] > v) { ts[j + 1] = ts[j]; j--; }
    ts[j + 1] = v;
  }
  for (int i = 0; i + 1 < nt; i++) {
    float ta = ts[i], tb = ts[i + 1];
    if (tb - ta < 0.0001f) continue;
    float tm = (ta + tb) * 0.5f;
    float mx = x0 + (x1 - x0) * tm, my = y0 + (y1 - y0) * tm;
    bool covered = false;
    for (A_long oi = 0; oi < occCount; oi++) {
      if (PointInQuad(&occs[oi], mx, my)) { covered = true; break; }
    }
    if (!covered) {
      DrawLine(output, clip, x0 + (x1 - x0) * ta, y0 + (y1 - y0) * ta,
               x0 + (x1 - x0) * tb, y0 + (y1 - y0) * tb, width, col);
    }
  }
}

static void DrawPolylineBehind(PF_LayerDef *output, const PF_Rect *clip,
                               const Pt *pts, A_long n, bool closed, float width,
                               const PF_Pixel8 *col, const OccQuad *occs, A_long occCount)
{
  if (n < 2) return;
  A_long spans = closed ? n : n - 1;
  for (A_long i = 0; i < spans; i++) {
    A_long j = closed ? (i + 1) % n : i + 1;
    DrawLineBehind(output, clip, pts[i].x, pts[i].y, pts[j].x, pts[j].y,
                   width, col, occs, occCount);
  }
}

static A_long MotionDrawCount(PF_InData *in_data, double t0, double t1, A_long n)
{
  return MotionPrefixCount(in_data, t0, t1, n);
}

static void DrawMarker(PF_LayerDef *output, const PF_Rect *clip,
                       float cx, float cy, float radius, int style,
                       const PF_Pixel8 *col)
{
  if (radius < 0.5f) radius = 0.5f;
  switch (style) {
    case 2: // square
      DrawSquare(output, clip, cx, cy, radius, col);
      break;
    case 3: { // triangle
      Pt p[3] = {{cx, cy - radius}, {cx + radius, cy + radius},
                 {cx - radius, cy + radius}};
      DrawPolyline(output, clip, p, 3, true, 1.4f, col);
      DrawDot(output, clip, cx, cy, radius * 0.28f, col);
      break;
    }
    case 4: { // diamond
      Pt p[4] = {{cx, cy - radius}, {cx + radius, cy},
                 {cx, cy + radius}, {cx - radius, cy}};
      DrawPolyline(output, clip, p, 4, true, 1.4f, col);
      DrawDot(output, clip, cx, cy, radius * 0.28f, col);
      break;
    }
    case 5: // cross
      DrawLine(output, clip, cx - radius, cy, cx + radius, cy, 1.5f, col);
      DrawLine(output, clip, cx, cy - radius, cx, cy + radius, 1.5f, col);
      DrawDot(output, clip, cx, cy, radius * 0.28f, col);
      break;
    default: // circle
      DrawDot(output, clip, cx, cy, radius, col);
      break;
  }
}

/* A selected custom layer is used as a small alpha-mask stamp. This keeps the
   feature independent of AEGP geometry access and lets users choose any
   existing shape/solid layer as the marker silhouette. */
static void DrawCustomMarker(PF_LayerDef *output, const PF_Rect *clip,
                             float cx, float cy, float radius,
                             const PF_LayerDef *custom, const PF_Pixel8 *col)
{
  if (!custom || !custom->data || custom->width <= 0 || custom->height <= 0) {
    DrawMarker(output, clip, cx, cy, radius, 1, col);
    return;
  }
  PF_Rect b = custom->extent_hint;
  if (b.left < 0) b.left = 0; if (b.top < 0) b.top = 0;
  if (b.right > custom->width) b.right = custom->width;
  if (b.bottom > custom->height) b.bottom = custom->height;
  if (b.right <= b.left || b.bottom <= b.top) {
    b.left = 0; b.top = 0; b.right = custom->width; b.bottom = custom->height;
  }
  float half = radius;
  int x0 = (int)floorf(cx - half - 1), x1 = (int)ceilf(cx + half + 1);
  int y0 = (int)floorf(cy - half - 1), y1 = (int)ceilf(cy + half + 1);
  if (x0 < clip->left) x0 = clip->left; if (y0 < clip->top) y0 = clip->top;
  if (x1 > clip->right) x1 = clip->right; if (y1 > clip->bottom) y1 = clip->bottom;
  for (int y = y0; y < y1; y++) {
    PF_Pixel8 *dst = OutRow(output, y);
    float v = ((float)y + 0.5f - (cy - half)) / (2.0f * half);
    if (v < 0 || v > 1) continue;
    int sy = b.top + (int)(v * (float)(b.bottom - b.top - 1));
    if (sy < b.top) sy = b.top; if (sy >= b.bottom) sy = b.bottom - 1;
    const PF_Pixel8 *src = (const PF_Pixel8 *)((const char *)custom->data +
                                               (size_t)sy * (size_t)custom->rowbytes);
    for (int x = x0; x < x1; x++) {
      float u = ((float)x + 0.5f - (cx - half)) / (2.0f * half);
      if (u < 0 || u > 1) continue;
      int sx = b.left + (int)(u * (float)(b.right - b.left - 1));
      if (sx < b.left) sx = b.left; if (sx >= b.right) sx = b.right - 1;
      float a = (float)src[sx].alpha / 255.0f;
      CompositeCover(&dst[x], col, a);
    }
  }
}

/* ================= render ================= */

static inline PF_Pixel8 *
SrcRow8(const PF_LayerDef *src, A_long y)
{
  return (PF_Pixel8 *)((char *)src->data + (size_t)y * (size_t)src->rowbytes);
}

static PF_Err
Render8(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef *params[], PF_LayerDef *output)
{
  PF_Err      err   = PF_Err_NONE;
  PF_LayerDef *src  = &params[PARAM_INPUT]->u.ld;

  // geometry blob baked during PF_Cmd_FRAME_SETUP (cached in frame_data).
  // In the GUI, FRAME_SETUP may arrive on a worker thread where AEGP is
  // skipped (see FrameSetup8); bake inline then — matchname probes are gone
  // and every AEGP call is error-checked and fails quietly.
  Blob *blob = (Blob *)in_data->frame_data;
  bool ownBlob = false;
  if (!blob && NeedGeoParams(params)) {
    blob = BakeBlob(in_data, params);
    ownBlob = (blob != NULL);
    DbgLog("R8: inline-bake blob=%p%s\n", (void *)blob,
           blob ? "" : " (alloc failed)");
  }
  GeoHeader *gh = (blob && ((GeoHeader *)blob->base)->magic == GEO_MAGIC)
                      ? (GeoHeader *)blob->base
                      : NULL;
  const bool belowMode = gh && (gh->status & 8);
  if (gh)
    DbgLog("R8: gh paths=%ld frames=%ld status=%ld own=%d\n",
           (long)gh->pathCount, (long)gh->frameN, (long)gh->status, (int)ownBlob);

  // ---- params ----
  PF_Pixel8 pxCol;
  pxCol.red   = params[PARAM_COLOR]->u.cd.value.red;
  pxCol.green = params[PARAM_COLOR]->u.cd.value.green;
  pxCol.blue  = params[PARAM_COLOR]->u.cd.value.blue;
  pxCol.alpha = 255;

  const float pxWidth  = (float)params[PARAM_WIDTH]->u.fs_d.value;
  const int   pxThr    = (int)params[PARAM_THRESHOLD]->u.sd.value;
  const bool  pxInvert = params[PARAM_INVERT]->u.bd.value != 0;
  const bool  onlyLine = params[PARAM_ONLY]->u.bd.value != 0;
  const bool  pxFade   = params[PARAM_FADE]->u.bd.value != 0;
  const int   pxMode   = (int)params[PARAM_MODE]->u.pd.value;

  const bool showFrame   = params[PARAM_SHOW_FRAME]->u.bd.value != 0;
  const bool showTFrame  = params[PARAM_SHOW_TFRAME]->u.bd.value != 0;
  const bool showMask    = params[PARAM_SHOW_MASK]->u.bd.value != 0;
  const bool showVerts   = params[PARAM_SHOW_VERTS]->u.bd.value != 0;
  const bool showHandles = params[PARAM_SHOW_HANDLES]->u.bd.value != 0;
  const bool showText    = params[PARAM_SHOW_TEXT]->u.bd.value != 0;
  const bool showShape   = params[PARAM_SHOW_SHAPE]->u.bd.value != 0;
  const bool showMotion  = params[PARAM_SHOW_MOTION]->u.bd.value != 0;
  const A_long thinN     = params[PARAM_THIN_N]->u.sd.value;
  const bool pxOn        = params[PARAM_PX_ON]->u.bd.value != 0;

  const float strokeW = (float)params[PARAM_STROKE_W]->u.fs_d.value;
  const float vertSize   = (float)params[PARAM_VERT_SIZE]->u.fs_d.value;
  const float handleSize = (float)params[PARAM_HANDLE_SIZE]->u.fs_d.value;
  const float handleLength = (float)params[PARAM_HANDLE_LENGTH]->u.fs_d.value / 100.0f;
  const int pointStyle = (int)params[PARAM_POINT_STYLE]->u.pd.value;
  PF_Pixel8 colFrame, colPath, colHandle, colMotion;
  #define READ_COL(ID, OUT) \
    OUT.red   = params[ID]->u.cd.value.red; \
    OUT.green = params[ID]->u.cd.value.green; \
    OUT.blue  = params[ID]->u.cd.value.blue; \
    OUT.alpha = 255;
  READ_COL(PARAM_COL_FRAME, colFrame);
  READ_COL(PARAM_COL_PATH, colPath);
  READ_COL(PARAM_COL_HANDLE, colHandle);
  READ_COL(PARAM_COL_MOTION, colMotion);

  PF_Rect r = output->extent_hint;
  if (r.left < 0) r.left = 0;
  if (r.top < 0) r.top = 0;
  if (r.right > src->width) r.right = src->width;
  if (r.bottom > src->height) r.bottom = src->height;
  if (r.right > output->width) r.right = output->width;
  if (r.bottom > output->height) r.bottom = output->height;

  const int x0 = r.left, y0 = r.top, x1 = r.right, y1 = r.bottom;
  const int w = x1 - x0, h = y1 - y0;
  if (w <= 0 || h <= 0) return PF_Err_NONE;

  // ---- base image ----
  if (!onlyLine) {
    err = PF_COPY(src, output, &r, &r);
    if (err) return err;
  } else {
    PF_Pixel8 clear = {0, 0, 0, 0};
    err = PF_FILL(&clear, &r, output);
    if (err) return err;
  }

  PF_ParamDef customPointLayer;
  AEFX_CLR_STRUCT(customPointLayer);
  bool customPointChecked = false;
  if (showVerts && pointStyle == 6) {
    if (!PF_CHECKOUT_PARAM(in_data, PARAM_CUSTOM_POINT_LAYER, in_data->current_time,
                           0, in_data->time_scale, &customPointLayer))
      customPointChecked = true;
  }

  // ---- geometry blob baked during PF_Cmd_FRAME_SETUP ----
  if (gh && gh->magic == GEO_MAGIC) {
      const XSample *track  = (const XSample *)(gh + 1);
      const Pt      *motion = (const Pt *)(track + gh->trackN);
      const PathDesc *paths = (const PathDesc *)(motion + gh->motionN);
      const Pt      *frames = (const Pt *)((const char *)gh + gh->frameBase);
      const OccQuad *occs   = (const OccQuad *)((const char *)gh + gh->occBase);
      const Pt      *flat   = (const Pt *)((const char *)gh + gh->flatBase);
      const KeyVert *keys   = (const KeyVert *)((const char *)gh + gh->keyBase);

      // Motion is rendered before the visualization overlays. In addition,
      // each trail is clipped against the current content quad of its own
      // moving layer, which makes it visually sit below that solid/shape/text.
      if (showMotion) {
        if (!belowMode && gh->motionN > 1) {
          OccQuad ownOcc;
          bool ownOccValid = !(gh->status & 4); // solids/footage are layer-space
          if (ownOccValid) {
            ownOcc.p[0] = (Pt){0, 0};
            ownOcc.p[1] = (Pt){(float)src->width, 0};
            ownOcc.p[2] = (Pt){(float)src->width, (float)src->height};
            ownOcc.p[3] = (Pt){0, (float)src->height};
          }
          const OccQuad *ownOccs = ownOccValid ? &ownOcc : NULL;
          A_long ownOccN = ownOccValid ? 1 : 0;
          A_long nDraw = MotionDrawCount(in_data, gh->t0, gh->t1, gh->motionN);
          if (gh->status & 4) {
            // vector/text buffers are already in comp space
            DrawPolylineBehind(output, &r, motion, nDraw, false, strokeW,
                               &colMotion, ownOccs, ownOccN);
          } else if (gh->trackN > 0) {
            // inverse-map parent-space motion points into layer space at current time
            double sec = NowSec(in_data, in_data->current_time);
            double ft = (sec - gh->t0) / (gh->t1 - gh->t0 + 1e-12) * (gh->trackN - 1);
            A_long i0 = (A_long)ft; if (i0 < 0) i0 = 0; if (i0 > gh->trackN - 2) i0 = gh->trackN - 2;
            double frac = ft - i0;
            const XSample *s0 = &track[i0], *s1 = &track[i0 + 1];
            float px_ = s0->px + (float)frac * (s1->px - s0->px);
            float py_ = s0->py + (float)frac * (s1->py - s0->py);
            float ax_ = s0->ax + (float)frac * (s1->ax - s0->ax);
            float ay_ = s0->ay + (float)frac * (s1->ay - s0->ay);
            float sx_ = (s0->sx + (float)frac * (s1->sx - s0->sx)) / 100.0f;
            float sy_ = (s0->sy + (float)frac * (s1->sy - s0->sy)) / 100.0f;
            float rot = (s0->rot + (float)frac * (s1->rot - s0->rot)) * (float)M_PI / 180.0f;
            float cr = cosf(-rot), sr = sinf(-rot);
            Pt mp[MOTION_SAMPLES + 1];
            A_long motionN = gh->motionN;
            if (motionN > MOTION_SAMPLES + 1) motionN = MOTION_SAMPLES + 1;
            for (A_long i = 0; i < motionN; i++) {
              float dx = motion[i].x - px_, dy = motion[i].y - py_;
              float x2 = dx * cr - dy * sr, y2 = dx * sr + dy * cr;
              if (sx_ != 0) x2 /= sx_;
              if (sy_ != 0) y2 /= sy_;
              mp[i].x = x2 + ax_; mp[i].y = y2 + ay_;
            }
            if (nDraw > motionN) nDraw = motionN;
            if (nDraw >= 2)
              DrawPolylineBehind(output, &r, mp, nDraw, false, strokeW,
                                 &colMotion, ownOccs, ownOccN);
          }
        }
        // Layers-below motion paths carry their own occluder index.
        if (gh->pathCount > 0) {
          for (A_long pi = 0; pi < gh->pathCount; pi++) {
            if (paths[pi].kind != 1) continue;
            A_long nDraw = MotionDrawCount(in_data, gh->t0, gh->t1, paths[pi].n);
            const OccQuad *pathOcc = NULL;
            A_long pathOccN = 0;
            if (paths[pi].occ >= 0 && paths[pi].occ < gh->occCount) {
              pathOcc = &occs[paths[pi].occ]; pathOccN = 1;
            }
            if (paths[pi].off >= 0 && nDraw >= 2 &&
                paths[pi].off + nDraw <= gh->flatTotal)
              DrawPolylineBehind(output, &r, flat + paths[pi].off, nDraw, false,
                                 strokeW, &colMotion, pathOcc, pathOccN);
          }
        }
      }

      // frame: per-target quads in layers-below mode; geometry bounds for vector
      // layers (comp-space buffer); layer rect otherwise; text layers only when
      // the Text Frame toggle is on
      bool isTextLayer = (gh->status & 16) != 0;
      if (showFrame && (belowMode || !isTextLayer ||
                        (showTFrame && gh->pathCount > 0))) {
        if (belowMode) {
          for (A_long fi = 0; fi < gh->frameN; fi++) {
            const Pt *q = frames + fi * 4;
            DrawPolyline(output, &r, q, 4, true, strokeW + 0.5f, &colFrame);
            float hs = 4.0f + strokeW;
            for (int i = 0; i < 4; i++) DrawSquare(output, &r, q[i].x, q[i].y, hs, &colFrame);
          }
        } else {
          Pt c[4];
          if (gh->pathCount > 0 && gh->flatTotal > 0) {
            float bx0 = 1e30f, by0 = 1e30f, bx1 = -1e30f, by1 = -1e30f;
            bool have = false;
            for (A_long pi = 0; pi < gh->pathCount; pi++) {
              const PathDesc *pd = &paths[pi];
              if (pd->kind == 1 && isTextLayer) continue;
              A_long cut = pd->kind == 1 ? MotionPrefixCount(in_data, gh->t0, gh->t1, pd->n) : pd->n;
              for (A_long fi = pd->off; fi < pd->off + cut && fi < gh->flatTotal; fi++) {
                if (flat[fi].x < bx0) bx0 = flat[fi].x; if (flat[fi].y < by0) by0 = flat[fi].y;
                if (flat[fi].x > bx1) bx1 = flat[fi].x; if (flat[fi].y > by1) by1 = flat[fi].y;
                have = true;
              }
            }
            if (!have) { bx0 = 0; by0 = 0; bx1 = (float)src->width; by1 = (float)src->height; }
            c[0].x = bx0; c[0].y = by0; c[1].x = bx1; c[1].y = by0;
            c[2].x = bx1; c[2].y = by1; c[3].x = bx0; c[3].y = by1;
          } else {
            float fw = (float)src->width, fh = (float)src->height;
            c[0].x = 0; c[0].y = 0; c[1].x = fw; c[1].y = 0;
            c[2].x = fw; c[2].y = fh; c[3].x = 0; c[3].y = fh;
          }
          DrawPolyline(output, &r, c, 4, true, strokeW + 0.5f, &colFrame);
          float hs = 4.0f + strokeW;
          for (int i = 0; i < 4; i++) DrawSquare(output, &r, c[i].x, c[i].y, hs, &colFrame);
        }
      }

      // structure paths (mask/shape/text), then dots and handles
      for (A_long pi = 0; pi < gh->pathCount; pi++) {
        if (paths[pi].kind != 0) continue;
        if (!(showText || showShape || showMask)) continue;
        if (paths[pi].off < 0 || paths[pi].n < 2 ||
            paths[pi].off + paths[pi].n > gh->flatTotal) continue;
        DrawPolyline(output, &r, flat + paths[pi].off, paths[pi].n,
                     paths[pi].closed != 0, strokeW, &colPath);
      }
      if ((showVerts || showHandles) && gh->keyCount > 0) {
        for (A_long ki = 0; ki < gh->keyCount; ki++) {
          const KeyVert *kv = &keys[ki];
          if (showVerts) {
            if (pointStyle == 6 && customPointChecked)
              DrawCustomMarker(output, &r, kv->x, kv->y, vertSize * 1.6f,
                               &customPointLayer.u.ld, &colPath);
            else
              DrawMarker(output, &r, kv->x, kv->y, vertSize, pointStyle, &colPath);
          }
          if (showHandles) {
            bool hasIn = fabsf(kv->tinX) + fabsf(kv->tinY) > 0.01f;
            bool hasOut = fabsf(kv->toutX) + fabsf(kv->toutY) > 0.01f;
            float inX = kv->tinX * handleLength, inY = kv->tinY * handleLength;
            float outX = kv->toutX * handleLength, outY = kv->toutY * handleLength;
            bool synthetic = (kv->flags & 2) != 0;
            float visibleHandleSize = synthetic ? fmaxf(3.0f, handleSize * 1.25f)
                                                : fmaxf(2.0f, handleSize);
            float visibleHandleWidth = synthetic ? fmaxf(1.6f, strokeW * 0.95f)
                                                 : fmaxf(1.0f, strokeW * 0.7f);
            if (hasIn) {
              DrawLine(output, &r, kv->x, kv->y, kv->x + inX, kv->y + inY,
                       visibleHandleWidth, &colHandle);
              if (synthetic) DrawMarker(output, &r, kv->x + inX, kv->y + inY,
                                        visibleHandleSize, 2, &colHandle);
              else DrawDot(output, &r, kv->x + inX, kv->y + inY, visibleHandleSize, &colHandle);
            }
            if (hasOut) {
              DrawLine(output, &r, kv->x, kv->y, kv->x + outX, kv->y + outY,
                       visibleHandleWidth, &colHandle);
              if (synthetic) DrawMarker(output, &r, kv->x + outX, kv->y + outY,
                                        visibleHandleSize, 2, &colHandle);
              else DrawDot(output, &r, kv->x + outX, kv->y + outY, visibleHandleSize, &colHandle);
            }
            // Text corners often have a mathematically zero tangent. Keep an
            // orange marker at those glyph vertices too, so mixed Latin/CJK
            // outlines expose every point consistently.
            if ((kv->flags & 1) && (!hasIn || !hasOut))
              DrawSquare(output, &r, kv->x, kv->y, visibleHandleSize * 0.65f, &colHandle);
          }
        }
      }
  }
  if (customPointChecked) PF_CHECKIN_PARAM(in_data, &customPointLayer);
  // frame fallback when no geometry blob was baked (e.g. only Frame enabled)
  if (showFrame && !gh) {
    float fw = (float)src->width, fh = (float)src->height;
    Pt c[4] = {{0, 0}, {fw, 0}, {fw, fh}, {0, fh}};
    DrawPolyline(output, &r, c, 4, true, strokeW + 0.5f, &colFrame);
    float hs = 4.0f + strokeW;
    for (int i = 0; i < 4; i++) DrawSquare(output, &r, c[i].x, c[i].y, hs, &colFrame);
  }
  if (ownBlob && blob) BlobFree(blob); // inline-baked; frame_data is freed in FRAME_SETDOWN

  // ---- pixel outline (Alpha / RGB luma / union) ----
  if (pxOn) {
    unsigned char *edge = (unsigned char *)malloc((size_t)w * (size_t)h);
    float         *dist = (float *)malloc(sizeof(float) * (size_t)w * (size_t)h);
    if (!edge || !dist) {
      if (edge) free(edge);
      if (dist) free(dist);
      return PF_Err_OUT_OF_MEMORY;
    }

    const int SW = src->width, SH = src->height;
    const bool useAlpha = (pxMode != 2);
    const bool useLuma  = (pxMode >= 2);
    for (int y = y0; y < y1; y++) {
      PF_Pixel8 *row = SrcRow8(src, y);
      PF_Pixel8 *rowPrev = (y > 0) ? SrcRow8(src, y - 1) : row;
      PF_Pixel8 *rowNext = (y < SH - 1) ? SrcRow8(src, y + 1) : row;
      for (int x = x0; x < x1; x++) {
        bool e = false;
        if (useAlpha) {
          int a = row[x].alpha;
          bool inside = pxInvert ? (a < pxThr) : (a >= pxThr);
          if (inside) {
            int al = (x > 0)      ? row[x - 1].alpha     : a;
            int ar = (x < SW - 1) ? row[x + 1].alpha     : a;
            int au = rowPrev[x].alpha;
            int ad = rowNext[x].alpha;
            bool nl = pxInvert ? (al < pxThr) : (al >= pxThr);
            bool nr = pxInvert ? (ar < pxThr) : (ar >= pxThr);
            bool nu = pxInvert ? (au < pxThr) : (au >= pxThr);
            bool nd = pxInvert ? (ad < pxThr) : (ad >= pxThr);
            e = !(nl && nr && nu && nd);
          }
        }
        if (useLuma) {
          const PF_Pixel8 &pc = row[x];
          int yc = (299 * pc.red + 587 * pc.green + 114 * pc.blue) / 1000;
          int yr = yc, yd = yc;
          if (x < SW - 1) {
            const PF_Pixel8 &pr = row[x + 1];
            yr = (299 * pr.red + 587 * pr.green + 114 * pr.blue) / 1000;
          }
          if (y < SH - 1) {
            const PF_Pixel8 &pd = rowNext[x];
            yd = (299 * pd.red + 587 * pd.green + 114 * pd.blue) / 1000;
          }
          int gx = yc - yr; if (gx < 0) gx = -gx;
          int gy = yc - yd; if (gy < 0) gy = -gy;
          bool le = (gx >= pxThr) || (gy >= pxThr);
          e = (pxMode == 2) ? le : (e || le);
        }
        edge[(size_t)(y - y0) * w + (x - x0)] = e ? 1 : 0;
      }
    }

    const float INF = 1e20f, D1 = 1.0f, D2 = 1.41421356f;
    for (int i = 0; i < w * h; i++) dist[i] = edge[i] ? 0.0f : INF;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t i = (size_t)y * w + x;
        float d = dist[i];
        if (x > 0) {
          float c = dist[i - 1] + D1;
          if (c < d) d = c;
          if (y > 0) { float c2 = dist[i - 1 - w] + D2; if (c2 < d) d = c2; }
        }
        if (y > 0) {
          float c = dist[i - w] + D1;
          if (c < d) d = c;
          if (x < w - 1) { float c2 = dist[i - w + 1] + D2; if (c2 < d) d = c2; }
        }
        dist[i] = d;
      }
    }
    for (int y = h - 1; y >= 0; y--) {
      for (int x = w - 1; x >= 0; x--) {
        size_t i = (size_t)y * w + x;
        float d = dist[i];
        if (x < w - 1) {
          float c = dist[i + 1] + D1;
          if (c < d) d = c;
          if (y < h - 1) { float c2 = dist[i + 1 + w] + D2; if (c2 < d) d = c2; }
        }
        if (y < h - 1) {
          float c = dist[i + w] + D1;
          if (c < d) d = c;
          if (x > 0) { float c2 = dist[i + w - 1] + D2; if (c2 < d) d = c2; }
        }
        dist[i] = d;
      }
    }

    for (int y = y0; y < y1; y++) {
      PF_Pixel8 *orow = OutRow(output, y);
      for (int x = x0; x < x1; x++) {
        float d = dist[(size_t)(y - y0) * w + (x - x0)];
        if (d > pxWidth) continue;
        float cover = pxFade ? (1.0f - d / (pxWidth + 1.0f)) : 1.0f;
        CompositeCover(&orow[x], &pxCol, cover);
      }
    }

    free(edge);
    free(dist);
  }

  return err;
}

extern "C" __attribute__((visibility("default"))) PF_Err
EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data,
           PF_ParamDef *params[], PF_LayerDef *output, void *extra)
{
  PF_Err err = PF_Err_NONE;
  switch (cmd) {
    case PF_Cmd_ABOUT:
      err = About(in_data, out_data, params, output);
      break;
    case PF_Cmd_GLOBAL_SETUP:
      err = GlobalSetup(in_data, out_data, params, output);
      break;
    case PF_Cmd_PARAMS_SETUP:
      err = ParamsSetup(in_data, out_data, params, output);
      break;
    case PF_Cmd_FRAME_SETUP:
      err = FrameSetup8(in_data, out_data, params, output);
      break;
    case PF_Cmd_RENDER:
      err = Render8(in_data, out_data, params, output);
      break;
    case PF_Cmd_FRAME_SETDOWN:
      err = FrameSetdown8(in_data, out_data, params, output);
      break;
    case PF_Cmd_UPDATE_PARAMS_UI:
      err = UpdateParamsUI(in_data, out_data, params, output);
      break;
    default:
      break;
  }
  return err;
}
