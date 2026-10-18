#ifndef CHARWEAVE_HOST_TRACE
#define CHARWEAVE_HOST_TRACE 0
#endif
/*
 * CharWeave Native 0.4.4 - macOS implementation
 *
 * This is a native After Effects effect plug-in.  The Windows package supplied
 * with this project contains no source, so this file re-implements the same
 * user-facing controls and keeps the original parameter IDs and text payload
 * format.  The fixed text payload is intentionally compatible with the
 * Windows build: TCW1 + UTF-16 text[257] + UTF-16 font[64] + reserved word.
 */

#import <AppKit/AppKit.h>
#import <CoreText/CoreText.h>
#import <CoreGraphics/CoreGraphics.h>
#import <Foundation/Foundation.h>
#import <dispatch/dispatch.h>

#include "AEConfig.h"
#include "AE_Effect.h"
#include "AE_EffectCB.h"
#include "AE_EffectSuites.h"
#include "AE_Macros.h"
#include "AE_EffectVers.h"
#include "AE_GeneralPlug.h"
#include "SPBasic.h"
#include "Param_Utils.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <cstdio>
#include <cstdlib>
#include <cstdarg>
#include <map>
#include <string>
#include <vector>

#ifndef M_PI
#define M_PI 3.141592653589793238462643383279502884
#endif

namespace cwmac {

#if CHARWEAVE_HOST_TRACE
static void HostTrace(const char *format, ...) {
  FILE *f = std::fopen("/tmp/charweave-host-trace.log", "a");
  if (!f) return;
  va_list args;
  va_start(args, format);
  std::vfprintf(f, format, args);
  va_end(args);
  std::fclose(f);
}
#else
static void HostTrace(const char *, ...) {}
#endif

static constexpr uint32_t kTextMagic = 0x31574354u; // bytes: 'T' 'C' 'W' '1'
static constexpr size_t kTextCapacity = 257;
static constexpr size_t kFontCapacity = 64;

#pragma pack(push, 1)
struct TextConfigRaw {
  uint32_t magic;
  uint16_t text[kTextCapacity];
  uint16_t font[kFontCapacity];
  uint16_t reserved;
};
#pragma pack(pop)

static_assert(sizeof(TextConfigRaw) == 0x288, "The Windows-compatible text blob must stay 0x288 bytes");
static constexpr uint32_t kSequenceMagic = 0x31535743u; // "CWS1"
struct SequenceConfig {
  uint32_t magic;
  uint32_t editedOnMac;
  TextConfigRaw text;
};
static_assert(sizeof(SequenceConfig) == sizeof(TextConfigRaw) + 8, "Flat sequence layout");

// These IDs match the supplied CharWeave.aex.  ID 1 is the hidden arbitrary
// data stream and ID 2 is the momentary edit button.
enum ParamID {
  P_TEXT_KEYFRAMES = 1,
  P_EDIT_TEXT = 2,
  P_PALETTE = 3,
  P_TEXT_MODE = 4,
  P_CELL_SIZE = 5,
  P_MAX_SIZE = 6,
  P_MIN_SIZE = 7,
  P_CONTRAST = 8,
  P_EXPOSURE = 9,
  P_GAMMA = 10,
  P_DARK_CUTOFF = 11,
  P_INVERT = 12,
  P_EDGE_BOOST = 13,
  P_SCATTER = 14,
  P_DRIFT_SPEED = 15,
  P_RANDOM_ROTATION = 16,
  P_RANDOM_SEED = 17,
  P_TRANSPARENT = 18,
  P_BACKGROUND = 19,
  P_LOW_COLOR = 20,
  P_HIGH_COLOR = 21,
  P_HIGHLIGHT_START = 22,
  P_DISPERSION_CENTER = 23,
  P_DISPERSION_PROGRESS = 24,
  P_INFLUENCE_RADIUS = 25,
  P_TRAVEL_DISTANCE = 26,
  P_FLUTTER_SPEED = 27,
  P_FLUTTER_AMOUNT = 28,
  P_DISPERSION_ROTATION = 29,
  P_SWIRL = 30,
  P_DISPERSION_FADE = 31,
  P_ORGANIC = 32,
  P_TURBULENCE_SCALE = 33,
  // Append new parameters: never renumber the Windows-compatible 1..33.
  P_GLYPH_SOURCE = 34,
  P_CREATE_STYLES = 35,
  P_STYLE_LAYER = 36,
  P_MANAGE_STYLES = 37,
  P_STYLE_COLOR = 38,
  P_STYLE_SORT = 39,
  PARAM_COUNT = 40
};

struct RGB {
  float r = 0.0f;
  float g = 0.0f;
  float b = 0.0f;
};

struct Sample {
  RGB color;
  float alpha = 0.0f;
  float luma = 0.0f;
};

struct RenderSettings {
  int glyphSource = 1; // Custom text (legacy default).
  int styleColor = 2;  // Keep RGBA from the shape/text/image layers.
  int styleSort = 2;   // Density order in Brightness map mode only.
  int palette = 3;
  int textMode = 1;
  float cellSize = 11.7f;
  float maxSize = 110.0f;
  float minSize = 25.0f;
  float contrast = 1.42f;
  float exposure = -0.03f;
  float gamma = 1.20f;
  float darkCutoff = 0.03f;
  bool invert = false;
  float edgeBoost = 0.0f;
  float scatter = 0.0f;
  float driftSpeed = 0.0f;
  float randomRotation = 0.0f;
  int randomSeed = 17;
  bool transparent = false;
  RGB background{0.82f, 0.02f, 0.08f};
  RGB lowColor{0.95f, 0.00f, 0.64f};
  RGB highColor{1.0f, 1.0f, 1.0f};
  float highlightStart = 0.70f;
  float centerX = 0.5f;
  float centerY = 0.5f;
  float dispersionProgress = 0.0f;
  float influenceRadius = 790.0f;
  float travelDistance = 500.0f;
  float flutterSpeed = 15.4f;
  float flutterAmount = 125.0f;
  float dispersionRotation = 90.0f;
  float swirl = 0.0f;
  float dispersionFade = 0.0f;
  float organic = 65.0f;
  float turbulenceScale = 180.0f;
  double timeSeconds = 0.0;
};

static inline float clamp01(float v) {
  return std::max(0.0f, std::min(1.0f, v));
}

static inline float clampf(float v, float lo, float hi) {
  return std::max(lo, std::min(hi, v));
}

static inline float smoothstep(float a, float b, float x) {
  if (a == b) return x < a ? 0.0f : 1.0f;
  const float t = clamp01((x - a) / (b - a));
  return t * t * (3.0f - 2.0f * t);
}

static inline float lerpf(float a, float b, float t) {
  return a + (b - a) * t;
}

static RGB mixRGB(const RGB &a, const RGB &b, float t) {
  t = clamp01(t);
  return {lerpf(a.r, b.r, t), lerpf(a.g, b.g, t), lerpf(a.b, b.b, t)};
}

static void SetDefaultText(TextConfigRaw &cfg) {
  std::memset(&cfg, 0, sizeof(cfg));
  cfg.magic = kTextMagic;
  const char16_t *text = u".:-=+*#%@";
  const char16_t *font = u"Microsoft YaHei";
  for (size_t i = 0; text[i] && i + 1 < kTextCapacity; ++i)
    cfg.text[i] = static_cast<uint16_t>(text[i]);
  for (size_t i = 0; font[i] && i + 1 < kFontCapacity; ++i)
    cfg.font[i] = static_cast<uint16_t>(font[i]);
}

static bool IsValidUTF16Unit(uint16_t c) {
  // The original Windows build explicitly rejects surrogate code units.  The
  // effect is intended for BMP glyphs and this also avoids half an emoji being
  // drawn as a separate cell.
  return !(c >= 0xD800 && c <= 0xDFFF);
}

static bool ValidateTextConfig(const TextConfigRaw &cfg) {
  if (cfg.magic != kTextMagic) return false;
  if (cfg.text[kTextCapacity - 1] != 0 || cfg.font[kFontCapacity - 1] != 0)
    return false;
  bool textTerminated = false;
  bool fontTerminated = false;
  bool textNonEmpty = false;
  bool fontNonEmpty = false;
  for (size_t i = 0; i < kTextCapacity; ++i) {
    if (!IsValidUTF16Unit(cfg.text[i])) return false;
    if (cfg.text[i] == 0) { textTerminated = true; break; }
    if (cfg.text[i] != u' ' && cfg.text[i] != u'\t' &&
        cfg.text[i] != u'\r' && cfg.text[i] != u'\n') textNonEmpty = true;
  }
  for (size_t i = 0; i < kFontCapacity; ++i) {
    if (!IsValidUTF16Unit(cfg.font[i])) return false;
    if (cfg.font[i] == 0) { fontTerminated = true; break; }
    if (cfg.font[i] != u' ' && cfg.font[i] != u'\t' &&
        cfg.font[i] != u'\r' && cfg.font[i] != u'\n') fontNonEmpty = true;
  }
  return textTerminated && fontTerminated && textNonEmpty && fontNonEmpty;
}

static std::u16string TextString(const TextConfigRaw &cfg) {
  size_t n = 0;
  while (n < kTextCapacity && cfg.text[n]) ++n;
  return std::u16string(reinterpret_cast<const char16_t *>(cfg.text),
                        reinterpret_cast<const char16_t *>(cfg.text) + n);
}

static std::u16string FontString(const TextConfigRaw &cfg) {
  size_t n = 0;
  while (n < kFontCapacity && cfg.font[n]) ++n;
  return std::u16string(reinterpret_cast<const char16_t *>(cfg.font),
                        reinterpret_cast<const char16_t *>(cfg.font) + n);
}

static PF_Handle NewHostHandle(PF_InData *in_data, size_t size) {
  if (!in_data || !in_data->utils || !in_data->utils->host_new_handle) return nullptr;
  return (*in_data->utils->host_new_handle)(static_cast<A_u_longlong>(size));
}

static void *LockHostHandle(PF_InData *in_data, PF_Handle handle) {
  if (!handle) return nullptr;
  if (in_data && in_data->utils && in_data->utils->host_lock_handle)
    return (*in_data->utils->host_lock_handle)(handle);
  return *handle;
}

static void UnlockHostHandle(PF_InData *in_data, PF_Handle handle) {
  if (!handle) return;
  if (in_data && in_data->utils && in_data->utils->host_unlock_handle)
    (*in_data->utils->host_unlock_handle)(handle);
}

static void DisposeHostHandle(PF_InData *in_data, PF_Handle handle) {
  if (!handle) return;
  if (in_data && in_data->utils && in_data->utils->host_dispose_handle)
    (*in_data->utils->host_dispose_handle)(handle);
}

static A_u_longlong HostHandleSize(PF_InData *in_data, PF_Handle handle) {
  if (!handle) return 0;
  if (in_data && in_data->utils && in_data->utils->host_get_handle_size)
    return (*in_data->utils->host_get_handle_size)(handle);
  return sizeof(TextConfigRaw);
}

static PF_Handle MakeTextHandle(PF_InData *in_data, const TextConfigRaw &cfg) {
  PF_Handle h = NewHostHandle(in_data, sizeof(TextConfigRaw));
  if (!h) return nullptr;
  void *p = LockHostHandle(in_data, h);
  if (!p) {
    DisposeHostHandle(in_data, h);
    return nullptr;
  }
  std::memcpy(p, &cfg, sizeof(cfg));
  UnlockHostHandle(in_data, h);
  return h;
}

static bool ReadTextHandle(PF_InData *in_data, PF_Handle h, TextConfigRaw &out) {
  if (!h || HostHandleSize(in_data, h) < sizeof(TextConfigRaw)) return false;
  void *p = LockHostHandle(in_data, h);
  if (!p) return false;
  std::memcpy(&out, p, sizeof(out));
  UnlockHostHandle(in_data, h);
  return ValidateTextConfig(out);
}

static bool ReadSequenceConfig(PF_InData *in_data, SequenceConfig &out) {
  if (!in_data || !in_data->sequence_data ||
      HostHandleSize(in_data, in_data->sequence_data) < sizeof(SequenceConfig))
    return false;
  // AE locks sequence_data before every callback (except the newly allocated
  // handle in SEQUENCE_SETUP), so never lock/unlock it again here.
  const void *p = *in_data->sequence_data;
  if (!p) return false;
  std::memcpy(&out, p, sizeof(out));
  return out.magic == kSequenceMagic && ValidateTextConfig(out.text);
}

static PF_Err NewSequenceConfig(PF_InData *in_data, PF_OutData *out_data) {
  SequenceConfig cfg{};
  cfg.magic = kSequenceMagic;
  cfg.editedOnMac = 0;
  SetDefaultText(cfg.text);
  PF_Handle h = NewHostHandle(in_data, sizeof(cfg));
  if (!h) return PF_Err_OUT_OF_MEMORY;
  void *p = LockHostHandle(in_data, h);
  if (!p) { DisposeHostHandle(in_data, h); return PF_Err_OUT_OF_MEMORY; }
  std::memcpy(p, &cfg, sizeof(cfg));
  UnlockHostHandle(in_data, h);
  out_data->sequence_data = h;
  HostTrace("SEQUENCE new handle=%p\n", h);
  return PF_Err_NONE;
}

static PF_Err CopyTextHandle(PF_InData *in_data, PF_Handle src, PF_Handle *dst) {
  if (!dst || !src || HostHandleSize(in_data, src) < sizeof(TextConfigRaw))
    return PF_Err_BAD_CALLBACK_PARAM;
  TextConfigRaw cfg;
  if (!ReadTextHandle(in_data, src, cfg)) return PF_Err_BAD_CALLBACK_PARAM;
  PF_Handle h = MakeTextHandle(in_data, cfg);
  if (!h) return PF_Err_OUT_OF_MEMORY;
  *dst = h;
  return PF_Err_NONE;
}

static PF_Err TextArbitrary(PF_InData *in_data, PF_ArbParamsExtra *extra) {
  if (!extra) return PF_Err_BAD_CALLBACK_PARAM;
  if (extra->id != P_TEXT_KEYFRAMES) return PF_Err_NONE;

  switch (extra->which_function) {
    case PF_Arbitrary_NEW_FUNC: {
      TextConfigRaw cfg;
      SetDefaultText(cfg);
      PF_Handle h = MakeTextHandle(in_data, cfg);
      if (!h) return PF_Err_OUT_OF_MEMORY;
      *extra->u.new_func_params.arbPH = h;
      return PF_Err_NONE;
    }
    case PF_Arbitrary_DISPOSE_FUNC:
      DisposeHostHandle(in_data, extra->u.dispose_func_params.arbH);
      return PF_Err_NONE;
    case PF_Arbitrary_COPY_FUNC:
      return CopyTextHandle(in_data, extra->u.copy_func_params.src_arbH,
                            extra->u.copy_func_params.dst_arbPH);
    case PF_Arbitrary_FLAT_SIZE_FUNC:
      if (!extra->u.flat_size_func_params.flat_data_sizePLu)
        return PF_Err_BAD_CALLBACK_PARAM;
      *extra->u.flat_size_func_params.flat_data_sizePLu = sizeof(TextConfigRaw);
      return PF_Err_NONE;
    case PF_Arbitrary_FLATTEN_FUNC: {
      if (extra->u.flatten_func_params.buf_sizeLu < sizeof(TextConfigRaw) ||
          !extra->u.flatten_func_params.flat_dataPV)
        return PF_Err_BAD_CALLBACK_PARAM;
      TextConfigRaw cfg;
      if (!ReadTextHandle(in_data, extra->u.flatten_func_params.arbH, cfg))
        return PF_Err_BAD_CALLBACK_PARAM;
      std::memcpy(extra->u.flatten_func_params.flat_dataPV, &cfg, sizeof(cfg));
      return PF_Err_NONE;
    }
    case PF_Arbitrary_UNFLATTEN_FUNC: {
      if (extra->u.unflatten_func_params.buf_sizeLu != sizeof(TextConfigRaw) ||
          !extra->u.unflatten_func_params.flat_dataPV ||
          !extra->u.unflatten_func_params.arbPH)
        return PF_Err_BAD_CALLBACK_PARAM;
      TextConfigRaw cfg;
      std::memcpy(&cfg, extra->u.unflatten_func_params.flat_dataPV, sizeof(cfg));
      if (!ValidateTextConfig(cfg)) return PF_Err_BAD_CALLBACK_PARAM;
      PF_Handle h = MakeTextHandle(in_data, cfg);
      if (!h) return PF_Err_OUT_OF_MEMORY;
      *extra->u.unflatten_func_params.arbPH = h;
      return PF_Err_NONE;
    }
    case PF_Arbitrary_INTERP_FUNC: {
      if (!extra->u.interp_func_params.interpPH) return PF_Err_BAD_CALLBACK_PARAM;
      TextConfigRaw left, right;
      if (!ReadTextHandle(in_data, extra->u.interp_func_params.left_arbH, left) ||
          !ReadTextHandle(in_data, extra->u.interp_func_params.right_arbH, right))
        return PF_Err_BAD_CALLBACK_PARAM;
      // Text keyframes are intentionally hold interpolation, not a character
      // morph.  At the exact right keyframe the new text becomes active.
      PF_Handle h = MakeTextHandle(in_data,
                                   extra->u.interp_func_params.tF >= 1.0 ? right : left);
      if (!h) return PF_Err_OUT_OF_MEMORY;
      *extra->u.interp_func_params.interpPH = h;
      return PF_Err_NONE;
    }
    case PF_Arbitrary_COMPARE_FUNC: {
      if (!extra->u.compare_func_params.compareP) return PF_Err_BAD_CALLBACK_PARAM;
      TextConfigRaw a, b;
      if (!ReadTextHandle(in_data, extra->u.compare_func_params.a_arbH, a) ||
          !ReadTextHandle(in_data, extra->u.compare_func_params.b_arbH, b))
        return PF_Err_BAD_CALLBACK_PARAM;
      *extra->u.compare_func_params.compareP =
          std::memcmp(&a, &b, sizeof(a)) == 0 ? PF_ArbCompare_EQUAL : PF_ArbCompare_NOT_EQUAL;
      return PF_Err_NONE;
    }
    case PF_Arbitrary_PRINT_SIZE_FUNC:
      if (!extra->u.print_size_func_params.print_sizePLu)
        return PF_Err_BAD_CALLBACK_PARAM;
      *extra->u.print_size_func_params.print_sizePLu = 256;
      return PF_Err_NONE;
    case PF_Arbitrary_PRINT_FUNC: {
      if (!extra->u.print_func_params.print_bufferPC ||
          extra->u.print_func_params.print_sizeLu == 0)
        return PF_Err_BAD_CALLBACK_PARAM;
      TextConfigRaw cfg;
      if (!ReadTextHandle(in_data, extra->u.print_func_params.arbH, cfg))
        return PF_Err_BAD_CALLBACK_PARAM;
      std::string text;
      const std::u16string t = TextString(cfg);
      for (char16_t c : t) text.push_back(c < 128 ? static_cast<char>(c) : '?');
      std::snprintf(extra->u.print_func_params.print_bufferPC,
                    extra->u.print_func_params.print_sizeLu,
                    "text=%s; font=CharWeave", text.c_str());
      return PF_Err_NONE;
    }
    case PF_Arbitrary_SCAN_FUNC:
      return PF_Err_CANNOT_PARSE_KEYFRAME_TEXT;
    default:
      return PF_Err_BAD_CALLBACK_PARAM;
  }
}

struct DialogState {
  TextConfigRaw *cfg = nullptr;
  bool accepted = false;
};

static NSString *NSStringFromUTF16(const uint16_t *chars, size_t capacity) {
  size_t n = 0;
  while (n < capacity && chars[n]) ++n;
  return [[NSString alloc] initWithCharacters:reinterpret_cast<const unichar *>(chars)
                                        length:n];
}

static void CopyNSStringToUTF16(NSString *value, uint16_t *dst, size_t capacity,
                                bool replaceSurrogates) {
  std::memset(dst, 0, capacity * sizeof(uint16_t));
  if (!value || capacity == 0) return;
  const NSUInteger n = [value length];
  const NSUInteger limit = std::min<NSUInteger>(n, capacity - 1);
  NSUInteger out = 0;
  for (NSUInteger i = 0; i < limit && out + 1 < capacity; ++i) {
    const unichar c = [value characterAtIndex:i];
    if (c >= 0xD800 && c <= 0xDFFF) {
      if (replaceSurrogates) dst[out++] = static_cast<uint16_t>('?');
      continue;
    }
    dst[out++] = static_cast<uint16_t>(c);
  }
  dst[out] = 0;
}

static bool RunEditDialog(TextConfigRaw &cfg) {
  HostTrace("DIALOG entered main=%d\n", [NSThread isMainThread] ? 1 : 0);
  @autoreleasepool {
    NSAlert *alert = [[NSAlert alloc] init];
    [alert setMessageText:@"CharWeave — Edit text and font"];
    [alert setInformativeText:@"Text supports up to 256 BMP UTF-16 characters. Emoji are replaced with ?. Enter a macOS font family or PostScript name."];
    [alert addButtonWithTitle:@"Cancel"];
    [alert addButtonWithTitle:@"OK"];

    NSView *view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 460, 92)];
    NSTextField *textLabel = [NSTextField labelWithString:@"Text"];
    NSTextField *fontLabel = [NSTextField labelWithString:@"Font"];
    NSTextField *textField = [NSTextField textFieldWithString:NSStringFromUTF16(cfg.text, kTextCapacity)];
    NSTextField *fontField = [NSTextField textFieldWithString:NSStringFromUTF16(cfg.font, kFontCapacity)];
    [textLabel setFrame:NSMakeRect(0, 56, 65, 24)];
    [fontLabel setFrame:NSMakeRect(0, 14, 65, 24)];
    [textField setFrame:NSMakeRect(72, 53, 388, 28)];
    [fontField setFrame:NSMakeRect(72, 11, 388, 28)];
    [textField setFont:[NSFont systemFontOfSize:14.0]];
    [fontField setFont:[NSFont systemFontOfSize:14.0]];
    [view addSubview:textLabel];
    [view addSubview:fontLabel];
    [view addSubview:textField];
    [view addSubview:fontField];
    [alert setAccessoryView:view];

    HostTrace("DIALOG before runModal\n");
    const NSModalResponse response = [alert runModal];
    HostTrace("DIALOG after runModal result=%ld\n", static_cast<long>(response));
    if (response != NSAlertSecondButtonReturn) return false;

    NSString *fontName = [fontField stringValue];
    if ([fontName length] == 0) fontName = @"Helvetica";
    CTFontRef testFont = CTFontCreateWithName((__bridge CFStringRef)fontName, 16.0, nullptr);
    if (!testFont) {
      NSAlert *warning = [[NSAlert alloc] init];
      [warning setMessageText:@"Font not found"];
      [warning setInformativeText:[NSString stringWithFormat:@"The font “%@” is not installed. The text was not changed.", fontName]];
      [warning addButtonWithTitle:@"OK"];
      [warning runModal];
      return false;
    }
    CFRelease(testFont);

    TextConfigRaw next = cfg;
    next.magic = kTextMagic;
    CopyNSStringToUTF16([textField stringValue], next.text, kTextCapacity, true);
    CopyNSStringToUTF16(fontName, next.font, kFontCapacity, true);
    if (!ValidateTextConfig(next)) {
      NSAlert *warning = [[NSAlert alloc] init];
      [warning setMessageText:@"Text cannot be empty"];
      [warning setInformativeText:@"Enter at least one visible character and a font name."];
      [warning addButtonWithTitle:@"OK"];
      [warning runModal];
      return false;
    }
    cfg = next;
    return true;
  }
}

static bool EditTextAndFont(TextConfigRaw &cfg) {
  __block bool accepted = false;
  if ([NSThread isMainThread]) {
    accepted = RunEditDialog(cfg);
  } else {
    dispatch_sync(dispatch_get_main_queue(), ^{
      accepted = RunEditDialog(cfg);
    });
  }
  return accepted;
}

static float ParamFloat(PF_ParamDef *params[], int id, float fallback) {
  if (!params || !params[id]) return fallback;
  return static_cast<float>(params[id]->u.fs_d.value);
}

static int ParamSlider(PF_ParamDef *params[], int id, int fallback) {
  if (!params || !params[id]) return fallback;
  return static_cast<int>(params[id]->u.sd.value);
}

static bool ParamBool(PF_ParamDef *params[], int id, bool fallback) {
  if (!params || !params[id]) return fallback;
  return params[id]->u.bd.value != 0;
}

static RGB ParamColor(PF_ParamDef *params[], int id, RGB fallback) {
  if (!params || !params[id]) return fallback;
  const PF_UnionablePixel &c = params[id]->u.cd.value;
  return {c.red / 255.0f, c.green / 255.0f, c.blue / 255.0f};
}

static float ParamPointX(PF_ParamDef *params[], int id, float fallback) {
  if (!params || !params[id]) return fallback;
  return static_cast<float>(params[id]->u.td.x_value) / 65536.0f;
}

static float ParamPointY(PF_ParamDef *params[], int id, float fallback) {
  if (!params || !params[id]) return fallback;
  return static_cast<float>(params[id]->u.td.y_value) / 65536.0f;
}

static RenderSettings ReadSettings(PF_InData *in_data, PF_ParamDef *params[]) {
  RenderSettings s;
  if (params && params[P_GLYPH_SOURCE] && params[P_GLYPH_SOURCE]->u.pd.value > 0)
    s.glyphSource = params[P_GLYPH_SOURCE]->u.pd.value;
  if (params && params[P_STYLE_COLOR] && params[P_STYLE_COLOR]->u.pd.value > 0)
    s.styleColor = params[P_STYLE_COLOR]->u.pd.value;
  if (params && params[P_STYLE_SORT] && params[P_STYLE_SORT]->u.pd.value > 0)
    s.styleSort = params[P_STYLE_SORT]->u.pd.value;
  s.palette = params[P_PALETTE] ? params[P_PALETTE]->u.pd.value : s.palette;
  s.textMode = params[P_TEXT_MODE] ? params[P_TEXT_MODE]->u.pd.value : s.textMode;
  s.cellSize = clampf(ParamFloat(params, P_CELL_SIZE, s.cellSize), 1.0f, 512.0f);
  s.maxSize = clampf(ParamFloat(params, P_MAX_SIZE, s.maxSize), 1.0f, 500.0f);
  s.minSize = clampf(ParamFloat(params, P_MIN_SIZE, s.minSize), 0.0f, 100.0f);
  s.contrast = clampf(ParamFloat(params, P_CONTRAST, s.contrast), 0.0f, 8.0f);
  s.exposure = clampf(ParamFloat(params, P_EXPOSURE, s.exposure), -8.0f, 8.0f);
  s.gamma = clampf(ParamFloat(params, P_GAMMA, s.gamma), 0.05f, 8.0f);
  s.darkCutoff = clamp01(ParamFloat(params, P_DARK_CUTOFF, s.darkCutoff));
  s.invert = ParamBool(params, P_INVERT, s.invert);
  s.edgeBoost = clampf(ParamFloat(params, P_EDGE_BOOST, s.edgeBoost), 0.0f, 4.0f);
  s.scatter = clampf(ParamFloat(params, P_SCATTER, s.scatter), 0.0f, 20.0f);
  s.driftSpeed = clampf(ParamFloat(params, P_DRIFT_SPEED, s.driftSpeed), 0.0f, 60.0f);
  s.randomRotation = clampf(ParamFloat(params, P_RANDOM_ROTATION, s.randomRotation), 0.0f, 360.0f);
  s.randomSeed = ParamSlider(params, P_RANDOM_SEED, s.randomSeed);
  s.transparent = ParamBool(params, P_TRANSPARENT, s.transparent);
  s.background = ParamColor(params, P_BACKGROUND, s.background);
  s.lowColor = ParamColor(params, P_LOW_COLOR, s.lowColor);
  s.highColor = ParamColor(params, P_HIGH_COLOR, s.highColor);
  s.highlightStart = clamp01(ParamFloat(params, P_HIGHLIGHT_START, s.highlightStart));
  s.centerX = ParamPointX(params, P_DISPERSION_CENTER, s.centerX);
  s.centerY = ParamPointY(params, P_DISPERSION_CENTER, s.centerY);
  s.dispersionProgress = clamp01(ParamFloat(params, P_DISPERSION_PROGRESS, 0.0f) / 100.0f);
  s.influenceRadius = clampf(ParamFloat(params, P_INFLUENCE_RADIUS, s.influenceRadius), 0.0f, 100000.0f);
  s.travelDistance = clampf(ParamFloat(params, P_TRAVEL_DISTANCE, s.travelDistance), 0.0f, 100000.0f);
  s.flutterSpeed = clampf(ParamFloat(params, P_FLUTTER_SPEED, s.flutterSpeed), 0.0f, 1000.0f);
  s.flutterAmount = clampf(ParamFloat(params, P_FLUTTER_AMOUNT, s.flutterAmount), 0.0f, 100000.0f);
  s.dispersionRotation = clampf(ParamFloat(params, P_DISPERSION_ROTATION, s.dispersionRotation), 0.0f, 360.0f);
  s.swirl = clampf(ParamFloat(params, P_SWIRL, s.swirl), -3600.0f, 3600.0f);
  s.dispersionFade = clamp01(ParamFloat(params, P_DISPERSION_FADE, s.dispersionFade) / 100.0f);
  s.organic = clamp01(ParamFloat(params, P_ORGANIC, s.organic) / 100.0f);
  s.turbulenceScale = clampf(ParamFloat(params, P_TURBULENCE_SCALE, s.turbulenceScale), 1.0f, 100000.0f);
  const float dsX = in_data && in_data->downsample_x.den > 0 && in_data->downsample_x.num > 0
      ? static_cast<float>(in_data->downsample_x.num) / in_data->downsample_x.den : 1.0f;
  const float dsY = in_data && in_data->downsample_y.den > 0 && in_data->downsample_y.num > 0
      ? static_cast<float>(in_data->downsample_y.num) / in_data->downsample_y.den : 1.0f;
  const float ds = std::min(dsX, dsY);
  s.cellSize *= ds;
  s.centerX *= dsX;
  s.centerY *= dsY;
  s.influenceRadius *= ds;
  s.travelDistance *= ds;
  s.flutterAmount *= ds;
  s.turbulenceScale *= ds;
  s.timeSeconds = in_data && in_data->time_scale
      ? static_cast<double>(in_data->current_time) / static_cast<double>(in_data->time_scale)
      : 0.0;
  return s;
}

static std::vector<std::u16string> TokenizeBMP(const std::u16string &text) {
  std::vector<std::u16string> out;
  for (size_t i = 0; i < text.size(); ++i) {
    char16_t c = text[i];
    if (c == u'\r' || c == u'\n' || c == u'\t') c = u' ';
    out.emplace_back(1, c);
  }
  if (out.empty()) out.emplace_back(1, u'#');
  return out;
}

struct StyleTexel {
  float r = 0, g = 0, b = 0, a = 0; // Premultiplied, linear interpolation safe.
};

struct GlyphMask {
  int width = 1;
  int height = 1;
  std::vector<uint8_t> alpha;
  std::vector<StyleTexel> stylePixels; // Empty for legacy CoreText glyphs.
  float density = 0.0f;
  int styleSlot = -1;
};

static GlyphMask MakeGlyphMask(const std::u16string &token,
                               const std::u16string &fontName,
                               float fontSize) {
  GlyphMask result;
  fontSize = clampf(fontSize, 1.0f, 256.0f);

  @autoreleasepool {
    CFStringRef cfName = CFStringCreateWithCharacters(kCFAllocatorDefault,
                                                        reinterpret_cast<const UniChar *>(fontName.data()),
                                                        static_cast<CFIndex>(fontName.size()));
    if (!cfName) {
      result.alpha.assign(1, 0);
      return result;
    }
    const UniChar unit = token.empty() ? static_cast<UniChar>('#')
                                       : static_cast<UniChar>(token[0]);
    CFStringRef cfToken = CFStringCreateWithCharacters(kCFAllocatorDefault, &unit, 1);
    CGGlyph glyph = 0;
    CTFontRef actualFont = CTFontCreateWithName(cfName, fontSize, nullptr);
    auto glyphAvailable = [&](CTFontRef font) -> bool {
      glyph = 0;
      return font && CTFontGetGlyphsForCharacters(font, &unit, &glyph, 1) && glyph != 0;
    };
    bool found = glyphAvailable(actualFont);
    if (!found) {
      // CoreText may return a non-null LastResort font for a Windows family
      // name (e.g. Microsoft YaHei) yet provide glyph ID 0 for every ASCII
      // character. Always test coverage before accepting a font object.
      static const CFStringRef fallbacks[] = {
        CFSTR("Helvetica"), CFSTR("PingFangSC-Regular"),
        CFSTR("HiraginoSansGB-W3"), CFSTR("Arial Unicode MS")
      };
      for (CFStringRef name : fallbacks) {
        CTFontRef candidate = CTFontCreateWithName(name, fontSize, nullptr);
        if (glyphAvailable(candidate)) {
          if (actualFont) CFRelease(actualFont);
          actualFont = candidate;
          found = true;
          break;
        }
        if (candidate) CFRelease(candidate);
      }
    }
    if (!found && cfToken) {
      CTFontRef systemFont = CTFontCreateUIFontForLanguage(kCTFontUIFontSystem,
                                                          fontSize, nullptr);
      CTFontRef candidate = systemFont
          ? CTFontCreateForString(systemFont, cfToken, CFRangeMake(0, 1)) : nullptr;
      if (glyphAvailable(candidate)) {
        if (actualFont) CFRelease(actualFont);
        actualFont = candidate;
        found = true;
      } else if (candidate) {
        CFRelease(candidate);
      }
      if (systemFont) CFRelease(systemFont);
    }
    if (!found) glyph = 0;

    CGRect bounds = CGRectZero;
    if (actualFont && glyph != 0)
      bounds = CTFontGetBoundingRectsForGlyphs(actualFont, kCTFontOrientationDefault, &glyph, nullptr, 1);
    HostTrace("GLYPH token=%04x base=%p actual=%p id=%u bounds=%.2f,%.2f,%.2f,%.2f\n",
              static_cast<unsigned>(unit), actualFont, actualFont, static_cast<unsigned>(glyph),
              bounds.origin.x, bounds.origin.y, bounds.size.width, bounds.size.height);
    const int pad = std::max(2, static_cast<int>(std::ceil(fontSize * 0.18f)));
    result.width = std::max(1, static_cast<int>(std::ceil(bounds.size.width)) + pad * 2);
    result.height = std::max(1, static_cast<int>(std::ceil(bounds.size.height)) + pad * 2);
    const int side = std::max(result.width, result.height);
    result.width = std::min(320, std::max(1, side));
    result.height = result.width;
    result.alpha.assign(static_cast<size_t>(result.width * result.height), 0);

    CGColorSpaceRef gray = CGColorSpaceCreateDeviceGray();
    CGContextRef ctx = gray ? CGBitmapContextCreate(nullptr, result.width, result.height,
                                                      8, result.width, gray,
                                                      kCGImageAlphaNone) : nullptr;
    HostTrace("CTX token=%04x width=%d gray=%p ctx=%p\n",
              static_cast<unsigned>(unit), result.width, gray, ctx);
    if (ctx && actualFont && glyph != 0) {
      CGContextSetAllowsAntialiasing(ctx, true);
      CGContextSetShouldAntialias(ctx, true);
      CGContextSetGrayFillColor(ctx, 1.0, 1.0);
      const CGPoint pos = CGPointMake((result.width - bounds.size.width) * 0.5 - bounds.origin.x,
                                      (result.height - bounds.size.height) * 0.5 - bounds.origin.y);
      CTFontDrawGlyphs(actualFont, &glyph, &pos, 1, ctx);
      const uint8_t *data = static_cast<const uint8_t *>(CGBitmapContextGetData(ctx));
      if (data) {
        for (int y0 = 0; y0 < result.height; ++y0) {
          const int srcY = result.height - 1 - y0;
          std::memcpy(&result.alpha[static_cast<size_t>(y0 * result.width)],
                      &data[static_cast<size_t>(srcY * result.width)],
                      static_cast<size_t>(result.width));
        }
      }
    }
    unsigned long inkThis = 0;
    for (uint8_t v : result.alpha) inkThis += v;
    HostTrace("INK token=%04x ink=%lu\n", static_cast<unsigned>(unit), inkThis);
    if (ctx) CGContextRelease(ctx);
    if (gray) CGColorSpaceRelease(gray);
    if (actualFont) CFRelease(actualFont);
    if (cfToken) CFRelease(cfToken);
    CFRelease(cfName);
  }
  return result;
}

struct GlyphAtlas {
  std::vector<std::u16string> tokens;
  std::vector<GlyphMask> masks;
  std::vector<size_t> tokenToMask;
};

static GlyphAtlas MakeAtlas(const TextConfigRaw &cfg, float maxFontSize) {
  GlyphAtlas atlas;
  atlas.tokens = TokenizeBMP(TextString(cfg));
  const std::u16string font = FontString(cfg);
  // Preserve first-seen order while avoiding duplicate CoreText work.
  std::map<std::u16string, size_t> seen;
  std::vector<std::u16string> unique;
  for (const auto &token : atlas.tokens) {
    if (seen.find(token) == seen.end()) {
      seen[token] = unique.size();
      unique.push_back(token);
    }
    atlas.tokenToMask.push_back(seen[token]);
  }
  atlas.masks.reserve(unique.size());
  for (const auto &token : unique)
    atlas.masks.push_back(MakeGlyphMask(token, font, maxFontSize));
  return atlas;
}

static size_t TokenIndex(const GlyphAtlas &atlas, const std::u16string &token) {
  for (size_t i = 0; i < atlas.tokens.size(); ++i)
    if (atlas.tokens[i] == token) return i;
  return 0;
}

static uint32_t Hash2(int x, int y, int seed) {
  uint32_t h = static_cast<uint32_t>(seed) ^ 0x9E3779B9u;
  h ^= static_cast<uint32_t>(x) * 0x85EBCA6Bu;
  h ^= static_cast<uint32_t>(y) * 0xC2B2AE35u;
  h ^= h >> 16;
  h *= 0x7FEB352Du;
  h ^= h >> 15;
  h *= 0x846CA68Bu;
  h ^= h >> 16;
  return h;
}

static float HashUnit(int x, int y, int seed, int channel = 0) {
  return (Hash2(x + channel * 101, y - channel * 67, seed) & 0x00FFFFFFu) / 16777215.0f;
}

static float ValueNoise(float x, float y, int seed) {
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float tx = x - x0;
  const float ty = y - y0;
  const float a = HashUnit(x0, y0, seed);
  const float b = HashUnit(x0 + 1, y0, seed);
  const float c = HashUnit(x0, y0 + 1, seed);
  const float d = HashUnit(x0 + 1, y0 + 1, seed);
  const float ux = tx * tx * (3.0f - 2.0f * tx);
  const float uy = ty * ty * (3.0f - 2.0f * ty);
  return lerpf(lerpf(a, b, ux), lerpf(c, d, ux), uy) * 2.0f - 1.0f;
}

static float MaskSample(const GlyphMask &mask, float x, float y) {
  if (mask.alpha.empty() || x < 0.0f || y < 0.0f || x >= mask.width - 1 || y >= mask.height - 1)
    return 0.0f;
  const int x0 = static_cast<int>(std::floor(x));
  const int y0 = static_cast<int>(std::floor(y));
  const float tx = x - x0;
  const float ty = y - y0;
  auto at = [&](int ix, int iy) {
    return mask.alpha[static_cast<size_t>(iy * mask.width + ix)] / 255.0f;
  };
  const float a = at(x0, y0);
  const float b = at(x0 + 1, y0);
  const float c = at(x0, y0 + 1);
  const float d = at(x0 + 1, y0 + 1);
  return lerpf(lerpf(a, b, tx), lerpf(c, d, tx), ty);
}

template <typename Pixel, int MaxValue>
static Sample ReadPixelAt(const PF_EffectWorld *world, int x, int y) {
  x = std::max(0, std::min(world->width - 1, x));
  y = std::max(0, std::min(world->height - 1, y));
  const auto *row = reinterpret_cast<const Pixel *>(
      reinterpret_cast<const uint8_t *>(world->data) + static_cast<size_t>(y) * world->rowbytes);
  const Pixel p = row[x];
  Sample s;
  s.color = {static_cast<float>(p.red) / MaxValue,
             static_cast<float>(p.green) / MaxValue,
             static_cast<float>(p.blue) / MaxValue};
  s.alpha = static_cast<float>(p.alpha) / MaxValue;
  if (s.alpha > 0.00001f) {
    s.color.r = clamp01(s.color.r / s.alpha);
    s.color.g = clamp01(s.color.g / s.alpha);
    s.color.b = clamp01(s.color.b / s.alpha);
  }
  s.luma = clamp01(0.299f * s.color.r + 0.587f * s.color.g + 0.114f * s.color.b);
  return s;
}

template <typename Pixel, int MaxValue>
static void SetPixel(Pixel &p, RGB color, float alpha) {
  alpha = clamp01(alpha);
  p.alpha = static_cast<decltype(p.alpha)>(std::lround(alpha * MaxValue));
  p.red = static_cast<decltype(p.red)>(std::lround(clamp01(color.r) * alpha * MaxValue));
  p.green = static_cast<decltype(p.green)>(std::lround(clamp01(color.g) * alpha * MaxValue));
  p.blue = static_cast<decltype(p.blue)>(std::lround(clamp01(color.b) * alpha * MaxValue));
}

template <typename Pixel, int MaxValue>
static void BlendPixel(Pixel &p, RGB color, float srcAlpha) {
  srcAlpha = clamp01(srcAlpha);
  if (srcAlpha <= 0.00001f) return;
  const float da = static_cast<float>(p.alpha) / MaxValue;
  const float dr = static_cast<float>(p.red) / MaxValue;
  const float dg = static_cast<float>(p.green) / MaxValue;
  const float db = static_cast<float>(p.blue) / MaxValue;
  const float keep = 1.0f - srcAlpha;
  const float oa = srcAlpha + da * keep;
  p.alpha = static_cast<decltype(p.alpha)>(std::lround(clamp01(oa) * MaxValue));
  p.red = static_cast<decltype(p.red)>(std::lround(clamp01(color.r * srcAlpha + dr * keep) * MaxValue));
  p.green = static_cast<decltype(p.green)>(std::lround(clamp01(color.g * srcAlpha + dg * keep) * MaxValue));
  p.blue = static_cast<decltype(p.blue)>(std::lround(clamp01(color.b * srcAlpha + db * keep) * MaxValue));
}

#include "StyleAtlas.inc"

template <typename Pixel, int MaxValue>
static PF_Err RenderTyped(PF_InData *in_data, PF_ParamDef *params[],
                          const PF_EffectWorld *src, PF_EffectWorld *dst,
                          const PF_EffectWorld *styleWorld = nullptr) {
  if (!dst || !dst->data || dst->width <= 0 || dst->height <= 0)
    return PF_Err_NONE;

  RenderSettings s = ReadSettings(in_data, params);
  TextConfigRaw textCfg;
  SetDefaultText(textCfg);
  if (params[P_TEXT_KEYFRAMES] &&
      !ReadTextHandle(in_data, params[P_TEXT_KEYFRAMES]->u.arb_d.value, textCfg)) {
    SetDefaultText(textCfg);
  }
  const float maxFont = clampf(s.cellSize * s.maxSize / 100.0f, 1.0f, 256.0f);
  const GlyphAtlas atlas = s.glyphSource == 2
      ? MakeStyleAtlas(styleWorld, maxFont, s.styleSort == 2 && s.textMode == 1)
      : MakeAtlas(textCfg, maxFont);
  unsigned long glyphInk = 0;
  for (const GlyphMask &mask : atlas.masks)
    for (uint8_t coverage : mask.alpha) glyphInk += coverage;
  HostTrace("ATLAS glyphs=%zu totalInk=%lu fontSize=%f src=%ldx%ld rowbytes=%ld flags=%ld dst=%ldx%ld rowbytes=%ld flags=%ld\n",
            atlas.masks.size(), glyphInk, maxFont,
            static_cast<long>(src ? src->width : -1), static_cast<long>(src ? src->height : -1),
            static_cast<long>(src ? src->rowbytes : -1), static_cast<long>(src ? src->world_flags : -1),
            static_cast<long>(dst->width), static_cast<long>(dst->height),
            static_cast<long>(dst->rowbytes), static_cast<long>(dst->world_flags));
  const int width = dst->width;
  const int height = dst->height;
  // Background pass.  Transparent mode deliberately clears rather than
  // copying the source; the source is used only as the brightness/color map.
  const RGB bg = s.palette == 1 ? RGB{0.015f, 0.090f, 0.53f}
               : s.palette == 2 ? RGB{0.005f, 0.007f, 0.025f}
               : s.background;
  for (int y = 0; y < height; ++y) {
    auto *row = reinterpret_cast<Pixel *>(reinterpret_cast<uint8_t *>(dst->data) +
                                          static_cast<size_t>(y) * dst->rowbytes);
    for (int x = 0; x < width; ++x)
      SetPixel<Pixel, MaxValue>(row[x], bg, s.transparent ? 0.0f : 1.0f);
    if (in_data && in_data->utils && in_data->inter.abort && (y & 15) == 0 && PF_ABORT(in_data))
      return PF_Interrupt_CANCEL;
  }

  if (atlas.masks.empty() || !src || !src->data || src->width <= 0 || src->height <= 0)
    return PF_Err_NONE; // The background pass above initialized all output pixels.

  const int cell = std::max(1, static_cast<int>(std::lround(s.cellSize)));
  const int cols = (width + cell - 1) / cell;
  const int rows = (height + cell - 1) / cell;
  const float centerX = s.centerX;
  const float centerY = s.centerY;
  const float activeRadius = std::max(0.0f, s.influenceRadius);
  const float progress = clamp01(s.dispersionProgress);
  const float waveRadius = activeRadius * progress;
  const float maxScatter = s.scatter * cell;
  const int glyphCount = static_cast<int>(atlas.tokens.size());
  unsigned long skipAlpha = 0, skipDark = 0, accepted = 0, blended = 0;
  if (src && src->data && src->width > 0 && src->height > 0) {
    const Sample center = ReadPixelAt<Pixel, MaxValue>(src, src->width / 2, src->height / 2);
    const Sample corner = ReadPixelAt<Pixel, MaxValue>(src, 0, 0);
    HostTrace("INPUT center=%f,%f,%f alpha=%f corner=%f,%f,%f alpha=%f\n",
      center.color.r,center.color.g,center.color.b,center.alpha,
      corner.color.r,corner.color.g,corner.color.b,corner.alpha);
  }

  for (int gy = 0; gy < rows; ++gy) {
    for (int gx = 0; gx < cols; ++gx) {
      const int sampleX = std::min(width - 1, gx * cell + cell / 2);
      const int sampleY = std::min(height - 1, gy * cell + cell / 2);
      Sample sample = ReadPixelAt<Pixel, MaxValue>(src, sampleX, sampleY);
      if (sample.alpha <= 0.001f) { ++skipAlpha; continue; }

      const Sample sx = ReadPixelAt<Pixel, MaxValue>(src, sampleX + 1, sampleY);
      const Sample sy = ReadPixelAt<Pixel, MaxValue>(src, sampleX, sampleY + 1);
      float brightness = sample.luma + s.edgeBoost *
          (std::fabs(sample.luma - sx.luma) + std::fabs(sample.luma - sy.luma));
      brightness = clamp01((brightness - 0.5f) * s.contrast + 0.5f);
      brightness = clamp01(brightness * std::pow(2.0f, s.exposure));
      brightness = clamp01(std::pow(std::max(0.0f, brightness), 1.0f / std::max(0.05f, s.gamma)));
      if (s.invert) brightness = 1.0f - brightness;
      if (brightness < s.darkCutoff) { ++skipDark; continue; }
      ++accepted;

      const uint32_t h = Hash2(gx, gy, s.randomSeed);
      float random01 = (h & 0xFFFFu) / 65535.0f;
      int glyphIndex = 0;
      if (s.textMode == 2) {
        glyphIndex = static_cast<int>(gy * cols + gx) % glyphCount;
      } else if (s.textMode == 3) {
        glyphIndex = static_cast<int>((h >> 8) % static_cast<uint32_t>(glyphCount));
      } else {
        glyphIndex = static_cast<int>(std::floor(brightness * (glyphCount - 1) + 0.5f));
      }
      glyphIndex = std::max(0, std::min(glyphCount - 1, glyphIndex));
      const GlyphMask &mask = atlas.masks[atlas.tokenToMask[static_cast<size_t>(glyphIndex)]];

      float px = gx * cell + cell * 0.5f;
      float py = gy * cell + cell * 0.5f;
      const float dx = px - centerX;
      const float dy = py - centerY;
      const float radius = std::sqrt(dx * dx + dy * dy);
      float influence = 0.0f;
      if (progress > 0.0f && activeRadius > 0.0f) {
        // A growing radial wave with a soft edge.  Once the wave has passed a
        // cell, the cell remains fully affected until the edge of the radius.
        const float wave = smoothstep(waveRadius - cell * 1.5f, waveRadius + cell * 1.5f, radius);
        const float inside = 1.0f - smoothstep(activeRadius - cell * 2.0f, activeRadius, radius);
        influence = clamp01((1.0f - wave) * inside);
      }

      float organicNoise = 0.0f;
      if (s.organic > 0.0f) {
        const float scale = std::max(1.0f, s.turbulenceScale);
        organicNoise = ValueNoise(px / scale + static_cast<float>(s.timeSeconds * s.flutterSpeed * 0.03),
                                  py / scale - static_cast<float>(s.timeSeconds * s.flutterSpeed * 0.02),
                                  s.randomSeed);
        influence = clamp01(influence * (1.0f + organicNoise * 0.35f * s.organic));
      }

      const float angle = std::atan2(dy, dx);
      const float dist = std::max(0.0001f, radius);
      const float nx = dx / dist;
      const float ny = dy / dist;
      const float tx = -ny;
      const float ty = nx;
      const float flutter = std::sin(static_cast<float>(s.timeSeconds * s.flutterSpeed) + random01 * 6.2831853f);
      const float drift = std::sin(static_cast<float>(s.timeSeconds * s.driftSpeed) + random01 * 6.2831853f);
      px += (HashUnit(gx, gy, s.randomSeed, 1) * 2.0f - 1.0f) * maxScatter;
      py += (HashUnit(gx, gy, s.randomSeed, 2) * 2.0f - 1.0f) * maxScatter;
      px += (nx * s.travelDistance * (1.0f + organicNoise * 0.35f * s.organic) + tx * flutter * s.flutterAmount) * influence;
      py += (ny * s.travelDistance * (1.0f + organicNoise * 0.35f * s.organic) + ty * flutter * s.flutterAmount) * influence;
      // Drift animates scattered cells even when radial dispersion is zero.
      px += tx * drift * maxScatter;
      py += ty * drift * maxScatter;

      const float swirlAngle = s.swirl * static_cast<float>(M_PI / 180.0) * influence;
      const float rx = dx * std::cos(swirlAngle) - dy * std::sin(swirlAngle);
      const float ry = dx * std::sin(swirlAngle) + dy * std::cos(swirlAngle);
      px += (rx - dx) * influence;
      py += (ry - dy) * influence;

      const float glyphSize = std::max(1.0f, maxFont *
          (s.minSize / 100.0f + (1.0f - s.minSize / 100.0f) * brightness));
      const float scale = glyphSize / maxFont;
      float rotation = 0.0f;
      if (s.randomRotation != 0.0f)
        rotation += (HashUnit(gx, gy, s.randomSeed, 3) * 2.0f - 1.0f) * s.randomRotation;
      if (s.dispersionRotation != 0.0f)
        rotation += (HashUnit(gx, gy, s.randomSeed, 4) * 2.0f - 1.0f) * s.dispersionRotation * influence;
      rotation *= static_cast<float>(M_PI / 180.0);

      RGB color;
      switch (s.palette) {
        case 1: // Blue / White
          color = mixRGB({0.03f, 0.24f, 0.90f}, {1.0f, 1.0f, 1.0f}, brightness);
          break;
        case 2: // Black / Blue / White
          color = brightness < 0.5f
              ? mixRGB({0.02f, 0.02f, 0.04f}, {0.03f, 0.24f, 0.90f}, brightness * 2.0f)
              : mixRGB({0.03f, 0.24f, 0.90f}, {1.0f, 1.0f, 1.0f}, (brightness - 0.5f) * 2.0f);
          break;
        case 4: // Source colors
          color = sample.color;
          break;
        case 3:
        default: {
          const float start = clampf(s.highlightStart, 0.0f, 0.999f);
          const float t = brightness <= start ? brightness / std::max(0.001f, start) * 0.5f
                                              : 0.5f + (brightness - start) / (1.0f - start) * 0.5f;
          color = mixRGB(s.lowColor, s.highColor, t);
          break;
        }
      }

      const int half = static_cast<int>(std::ceil(std::max(mask.width, mask.height) * scale * 0.72f));
      const int x0 = std::max(0, static_cast<int>(std::floor(px)) - half - 1);
      const int x1 = std::min(width - 1, static_cast<int>(std::floor(px)) + half + 1);
      const int y0 = std::max(0, static_cast<int>(std::floor(py)) - half - 1);
      const int y1 = std::min(height - 1, static_cast<int>(std::floor(py)) + half + 1);
      const float ca = std::cos(rotation);
      const float sa = std::sin(rotation);
      const float fade = 1.0f - s.dispersionFade * influence;
      for (int y = y0; y <= y1; ++y) {
        auto *outRow = reinterpret_cast<Pixel *>(reinterpret_cast<uint8_t *>(dst->data) +
                                                  static_cast<size_t>(y) * dst->rowbytes);
        for (int x = x0; x <= x1; ++x) {
          const float ox = (x + 0.5f - px) / std::max(0.001f, scale);
          const float oy = (y + 0.5f - py) / std::max(0.001f, scale);
          const float ux = (ca * ox + sa * oy) + mask.width * 0.5f;
          const float uy = (-sa * ox + ca * oy) + mask.height * 0.5f;
          const StyleTexel style = mask.stylePixels.empty()
              ? StyleTexel{} : StyleSample(mask, ux, uy);
          const float coverage = mask.stylePixels.empty() ? MaskSample(mask, ux, uy) : style.a;
          if (coverage <= 0.001f) continue;
          const RGB ink = s.styleColor == 2 && !mask.stylePixels.empty()
              ? RGB{clamp01(style.r / coverage), clamp01(style.g / coverage), clamp01(style.b / coverage)}
              : color;
          const float a = coverage * fade * sample.alpha;
          BlendPixel<Pixel, MaxValue>(outRow[x], ink, a);
          ++blended;
        }
      }
    }
    if (in_data && in_data->utils && in_data->inter.abort && PF_ABORT(in_data))
      return PF_Interrupt_CANCEL;
  }
  HostTrace("STATS cells=%d accepted=%lu skipAlpha=%lu skipDark=%lu blended=%lu\n",
            cols * rows, accepted, skipAlpha, skipDark, blended);
  return PF_Err_NONE;
}

#include "StyleCheckout.inc"

static PF_Err About(PF_InData *in_data, PF_OutData *out_data, PF_ParamDef **, PF_LayerDef *) {
  PF_STRCPY(out_data->return_msg, "CharWeave Native 0.4.4 beta 2 macOS; custom text and shape/text/image layer styles with organic dispersion.");
  return PF_Err_NONE;
}

static PF_Err GlobalSetup(PF_InData *, PF_OutData *out_data, PF_ParamDef **, PF_LayerDef *) {
  out_data->my_version = PF_VERSION(0, 4, 4, PF_Stage_BETA, 2);
  // Keep the PiPL flags in sync. Time-driven drift requires NON_PARAM_VARY;
  // all persistent text lives in the arbitrary parameter, not sequence data.
  out_data->out_flags = PF_OutFlag_NON_PARAM_VARY | PF_OutFlag_DEEP_COLOR_AWARE;
  // Native scripting bridge disabled after host verification failures.
  // The companion Styles Manager provides creation and automatic layout.
  // Do not advertise threaded rendering: CoreText objects are created per
  // render, but keeping the host-side behavior conservative matches the
  // supplied Windows plug-in.
  out_data->out_flags2 = PF_OutFlag2_NONE;
  return PF_Err_NONE;
}

static PF_Err ParamsSetup(PF_InData *in_data, PF_OutData *out_data,
                          PF_ParamDef *params[], PF_LayerDef *) {
  PF_Err err = PF_Err_NONE;
  PF_ParamDef def;
  AEFX_CLR_STRUCT(def);

  TextConfigRaw defaults;
  SetDefaultText(defaults);
  PF_Handle defaultHandle = MakeTextHandle(in_data, defaults);
  if (!defaultHandle) return PF_Err_OUT_OF_MEMORY;
  AEFX_CLR_STRUCT(def);
  PF_ADD_ARBITRARY2("Text keyframes", 0, 0, PF_ParamFlag_NONE, PF_PUI_NO_ECW_UI,
                    defaultHandle, P_TEXT_KEYFRAMES, nullptr);

  // PF_ADD_BUTTON does not add SUPERVISE itself. Without it AE never sends
  // USER_CHANGED_PARAM, so the text editor appears to do nothing.
  AEFX_CLR_STRUCT(def);
  PF_ADD_BUTTON("Custom text", "Edit text and font...", PF_PUI_NONE,
                PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, P_EDIT_TEXT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Palette", 4, 3,
               "Blue / White|Black / Blue / White|Custom colors|Source colors|", P_PALETTE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Text mode", 3, 1,
               "Brightness map|Repeat in order|Stable random|", P_TEXT_MODE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Cell size (px)", 1.0, 200.0, 2.0, 60.0, 11.7, 1, 0, 0, P_CELL_SIZE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Maximum size (%)", 1.0, 400.0, 20.0, 180.0, 110.0, 1, 0, 0, P_MAX_SIZE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Minimum size (%)", 0.0, 100.0, 0.0, 100.0, 25.0, 1, 0, 0, P_MIN_SIZE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Contrast", 0.0, 4.0, 0.0, 3.0, 1.42, 2, 0, 0, P_CONTRAST);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Exposure", -4.0, 4.0, -2.0, 2.0, -0.03, 2, 0, 0, P_EXPOSURE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Gamma", 0.05, 4.0, 0.1, 3.0, 1.20, 2, 0, 0, P_GAMMA);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Dark cutoff", 0.0, 1.0, 0.0, 1.0, 0.03, 2, 0, 0, P_DARK_CUTOFF);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX("Invert brightness", "", 0, 0, P_INVERT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Edge boost", 0.0, 4.0, 0.0, 2.0, 0.0, 2, 0, 0, P_EDGE_BOOST);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Scatter (cells)", 0.0, 20.0, 0.0, 5.0, 0.0, 2, 0, 0, P_SCATTER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Drift speed", 0.0, 60.0, 0.0, 20.0, 0.0, 2, 0, 0, P_DRIFT_SPEED);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Random rotation", 0.0, 360.0, 0.0, 180.0, 0.0, 1, 0, 0, P_RANDOM_ROTATION);
  AEFX_CLR_STRUCT(def);
  PF_ADD_SLIDER("Random seed", 0, 9999, 0, 9999, 17, P_RANDOM_SEED);
  AEFX_CLR_STRUCT(def);
  PF_ADD_CHECKBOX("Transparent background", "", 0, 0, P_TRANSPARENT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_COLOR("Background", 210, 14, 28, P_BACKGROUND);
  AEFX_CLR_STRUCT(def);
  PF_ADD_COLOR("Low color", 242, 0, 164, P_LOW_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_COLOR("High color", 255, 255, 255, P_HIGH_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Highlight start", 0.0, 1.0, 0.0, 1.0, 0.70, 2, 0, 0, P_HIGHLIGHT_START);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POINT("Dispersion center", 50, 50, 0, P_DISPERSION_CENTER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Dispersion progress (%)", 0.0, 100.0, 0.0, 100.0, 0.0, 1, 0, 0, P_DISPERSION_PROGRESS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Influence radius (px)", 0.0, 100000.0, 0.0, 5000.0, 790.0, 1, 0, 0, P_INFLUENCE_RADIUS);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Travel distance (px)", 0.0, 100000.0, 0.0, 5000.0, 500.0, 1, 0, 0, P_TRAVEL_DISTANCE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Flutter speed", 0.0, 1000.0, 0.0, 60.0, 15.4, 1, 0, 0, P_FLUTTER_SPEED);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Flutter amount (px)", 0.0, 100000.0, 0.0, 1000.0, 125.0, 1, 0, 0, P_FLUTTER_AMOUNT);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Dispersion rotation", 0.0, 360.0, 0.0, 180.0, 90.0, 1, 0, 0, P_DISPERSION_ROTATION);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Swirl (degrees)", -3600.0, 3600.0, -360.0, 360.0, 0.0, 1, 0, 0, P_SWIRL);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Dispersion fade (%)", 0.0, 100.0, 0.0, 100.0, 0.0, 1, 0, 0, P_DISPERSION_FADE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Organic distortion (%)", 0.0, 100.0, 0.0, 100.0, 65.0, 1, 0, 0, P_ORGANIC);
  AEFX_CLR_STRUCT(def);
  PF_ADD_FLOAT_SLIDERX("Turbulence scale (px)", 1.0, 10000.0, 10.0, 1000.0, 180.0, 1, 0, 0, P_TURBULENCE_SCALE);

  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Glyph source", 2, 1, "Custom text|Layer style library|", P_GLYPH_SOURCE);
  AEFX_CLR_STRUCT(def);
  PF_ADD_BUTTON("Create style precomp", "Use Styles Manager", PF_PUI_DISABLED,
                PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, P_CREATE_STYLES);
  AEFX_CLR_STRUCT(def);
  PF_ADD_LAYER("Style library layer", PF_LayerDefault_NONE, P_STYLE_LAYER);
  AEFX_CLR_STRUCT(def);
  PF_ADD_BUTTON("Manage layer styles", "Use Styles Manager", PF_PUI_DISABLED,
                PF_ParamFlag_SUPERVISE | PF_ParamFlag_CANNOT_TIME_VARY, P_MANAGE_STYLES);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Style colors", 2, 2, "Use palette|Keep layer colors|", P_STYLE_COLOR);
  AEFX_CLR_STRUCT(def);
  PF_ADD_POPUP("Brightness style order", 2, 2, "Layer order|Ink density|", P_STYLE_SORT);

  out_data->num_params = PARAM_COUNT;
  return err;
}

#include "StyleLibraryUI.inc"

static PF_Err UserChangedParam(PF_InData *in_data, PF_OutData *out_data,
                               PF_ParamDef *params[], PF_LayerDef *, void *extra) {
  if (!extra) return PF_Err_NONE;
  const PF_UserChangedParamExtra *changed = static_cast<const PF_UserChangedParamExtra *>(extra);
  HostTrace("USER_CHANGED index=%ld main=%d\n", static_cast<long>(changed->param_index),
            [NSThread isMainThread] ? 1 : 0);
  if (changed->param_index == P_CREATE_STYLES) return PF_Err_NONE;
  if (changed->param_index == P_MANAGE_STYLES) return PF_Err_NONE;
  if (changed->param_index != P_EDIT_TEXT || !params[P_TEXT_KEYFRAMES]) return PF_Err_NONE;

  TextConfigRaw cfg;
  SetDefaultText(cfg);
  HostTrace("USER before ReadTextHandle value=%p\n", params[P_TEXT_KEYFRAMES]->u.arb_d.value);
  bool configOk = ReadTextHandle(in_data, params[P_TEXT_KEYFRAMES]->u.arb_d.value, cfg);
  HostTrace("USER after ReadTextHandle ok=%d\n", configOk ? 1 : 0);
  HostTrace("USER before EditTextAndFont\n");
  if (!EditTextAndFont(cfg)) { HostTrace("USER dialog cancelled\n"); return PF_Err_NONE; }
  HostTrace("USER after EditTextAndFont text0=%04x text1=%04x font0=%04x\n",
            static_cast<unsigned>(cfg.text[0]), static_cast<unsigned>(cfg.text[1]),
            static_cast<unsigned>(cfg.font[0]));

  PF_Handle oldHandle = params[P_TEXT_KEYFRAMES]->u.arb_d.value;
  HostTrace("USER before HostHandleSize old=%p\n", oldHandle);
  const A_u_longlong oldSize = oldHandle ? HostHandleSize(in_data, oldHandle) : 0;
  HostTrace("USER after HostHandleSize size=%llu\n", static_cast<unsigned long long>(oldSize));
  if (oldHandle && oldSize >= sizeof(TextConfigRaw)) {
    // AE owns the arbitrary-data handle.  Update it in place, matching the
    // supplied Windows plug-in and avoiding a host-side double dispose.
    HostTrace("USER before LockHostHandle\n");
    void *p = LockHostHandle(in_data, oldHandle);
    HostTrace("USER after LockHostHandle ptr=%p\n", p);
    if (!p) return PF_Err_BAD_CALLBACK_PARAM;
    std::memcpy(p, &cfg, sizeof(cfg));
    HostTrace("USER after memcpy\n");
    UnlockHostHandle(in_data, oldHandle);
    HostTrace("USER after unlock\n");
  } else {
    HostTrace("USER before MakeTextHandle replacement\n");
    PF_Handle replacement = MakeTextHandle(in_data, cfg);
    HostTrace("USER after MakeTextHandle replacement=%p\n", replacement);
    if (!replacement) return PF_Err_OUT_OF_MEMORY;
    params[P_TEXT_KEYFRAMES]->u.arb_d.value = replacement;
  }
  params[P_TEXT_KEYFRAMES]->uu.change_flags = PF_ChangeFlag_CHANGED_VALUE;
  out_data->out_flags |= PF_OutFlag_FORCE_RERENDER | PF_OutFlag_REFRESH_UI;
  HostTrace("USER changed-value flag set\n");
  return PF_Err_NONE;
}

} // namespace cwmac

extern "C" __attribute__((visibility("default"))) PF_Err
EffectMain(PF_Cmd cmd, PF_InData *in_data, PF_OutData *out_data,
           PF_ParamDef *params[], PF_LayerDef *output, void *extra) {
  using namespace cwmac;
  if (cmd == PF_Cmd_USER_CHANGED_PARAM || cmd == PF_Cmd_EVENT || cmd == PF_Cmd_DO_DIALOG)
    HostTrace("COMMAND selector=%ld extra=%p\n", static_cast<long>(cmd), extra);
  switch (cmd) {
    case PF_Cmd_ABOUT:
      return About(in_data, out_data, params, output);
    case PF_Cmd_GLOBAL_SETUP:
      return GlobalSetup(in_data, out_data, params, output);
    case PF_Cmd_PARAMS_SETUP:
      return ParamsSetup(in_data, out_data, params, output);
    case PF_Cmd_SEQUENCE_SETUP:
      return NewSequenceConfig(in_data, out_data);
    case PF_Cmd_SEQUENCE_RESETUP: {
      SequenceConfig saved{};
      return ReadSequenceConfig(in_data, saved) ? PF_Err_NONE
                                               : NewSequenceConfig(in_data, out_data);
    }
    case PF_Cmd_SEQUENCE_SETDOWN:
      DisposeHostHandle(in_data, in_data->sequence_data);
      return PF_Err_NONE;
    case PF_Cmd_USER_CHANGED_PARAM:
      return UserChangedParam(in_data, out_data, params, output, extra);
    case PF_Cmd_UPDATE_PARAMS_UI:
      return PF_Err_NONE; // No AEGP or script calls in the effect callback.
    case PF_Cmd_ARBITRARY_CALLBACK:
      return TextArbitrary(in_data, static_cast<PF_ArbParamsExtra *>(extra));
    case PF_Cmd_RENDER: {
      const char *tracePath = std::getenv("CHARWEAVE_TRACE_FILE");
      if (tracePath) {
        FILE *f = std::fopen(tracePath, "a");
        if (f) {
          std::fprintf(f, "BEGIN t=%ld/%lu input=%ldx%ld data=%p output=%ldx%ld data=%p deep=%d\n",
                       static_cast<long>(in_data->current_time), static_cast<unsigned long>(in_data->time_scale),
                       static_cast<long>(params[0]->u.ld.width), static_cast<long>(params[0]->u.ld.height),
                       params[0]->u.ld.data, static_cast<long>(output->width),
                       static_cast<long>(output->height), output->data, PF_WORLD_IS_DEEP(output) ? 1 : 0);
          std::fclose(f);
        }
      }
      PF_Err renderErr = RenderFrame(in_data, out_data, params, output);
      if (tracePath) {
        FILE *f = std::fopen(tracePath, "a");
        if (f) { std::fprintf(f, "END err=%ld\n", static_cast<long>(renderErr)); std::fclose(f); }
      }
      return renderErr;
    }
    default:
      return PF_Err_NONE;
  }
}
