// GDI objects must be deselected before release. DIB bits are only read after
// flushing the thread's queued GDI drawing. Kept separate for lifecycle tests.
static GlyphMask MakeGlyphMask(const std::u16string &token,
                               const std::u16string &fontName, float fontSize) {
  GlyphMask result;
  result.alpha.assign(1, 0);
  fontSize = clampf(fontSize, 1.0f, 256.0f);
  const wchar_t unit = token.empty() ? L'#' : static_cast<wchar_t>(token[0]);
  const std::wstring requested(fontName.begin(), fontName.end());
  const std::vector<std::wstring> fonts = {
      requested, L"Microsoft YaHei", L"Arial", L"Segoe UI Symbol"};
  HDC dc = CreateCompatibleDC(nullptr);
  if (!dc) return result;

  HFONT chosen = nullptr;
  HGDIOBJ oldFont = nullptr;
  HBITMAP bitmap = nullptr;
  HGDIOBJ oldBitmap = nullptr;
  auto cleanup = [&] {
    if (oldBitmap) SelectObject(dc, oldBitmap);
    if (oldFont) SelectObject(dc, oldFont);
    DeleteDC(dc);
    if (bitmap) DeleteObject(bitmap);
    if (chosen) DeleteObject(chosen);
  };
  SIZE extent{};
  TEXTMETRICW metrics{};
  for (const auto &name : fonts) {
    if (name.empty()) continue;
    HFONT font = CreateFontW(static_cast<int>(std::lround(fontSize)), 0, 0, 0,
        FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_TT_ONLY_PRECIS,
        CLIP_DEFAULT_PRECIS, ANTIALIASED_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        name.c_str());
    if (!font) continue;
    HGDIOBJ previous = SelectObject(dc, font);
    if (!previous) { DeleteObject(font); continue; }
    WORD glyph = 0xffff;
    const bool available = GetGlyphIndicesW(dc, &unit, 1, &glyph,
        GGI_MARK_NONEXISTING_GLYPHS) == 1 && glyph != 0xffff && glyph != 0;
    if (available && GetTextMetricsW(dc, &metrics) &&
        GetTextExtentPoint32W(dc, &unit, 1, &extent) &&
        extent.cx > 0 && metrics.tmHeight > 0) {
      chosen = font;
      oldFont = previous;
      break;
    }
    SelectObject(dc, previous);
    DeleteObject(font);
  }
  if (!chosen) { cleanup(); return result; }

  const int pad = std::max(2, static_cast<int>(std::ceil(fontSize * .18f)));
  const int side = std::min(320, std::max(1,
      std::max(static_cast<int>(extent.cx), static_cast<int>(metrics.tmHeight)) + pad * 2));
  result.width = result.height = side;
  result.alpha.assign(static_cast<size_t>(side * side), 0);
  BITMAPINFO info{};
  info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  info.bmiHeader.biWidth = side;
  info.bmiHeader.biHeight = -side;
  info.bmiHeader.biPlanes = 1;
  info.bmiHeader.biBitCount = 32;
  info.bmiHeader.biCompression = BI_RGB;
  void *bits = nullptr;
  bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
  if (bitmap && bits) {
    oldBitmap = SelectObject(dc, bitmap);
    if (oldBitmap) {
      std::memset(bits, 0, static_cast<size_t>(side * side) * sizeof(uint32_t));
      SetBkMode(dc, TRANSPARENT);
      SetTextColor(dc, RGB(255, 255, 255));
      const BOOL drawn = TextOutW(dc, (side - extent.cx) / 2,
          (side - metrics.tmHeight) / 2, &unit, 1);
      const BOOL flushed = GdiFlush();
      if (drawn && flushed) {
        const auto *pixels = static_cast<const uint32_t *>(bits);
        for (size_t i = 0; i < result.alpha.size(); ++i) {
          const uint32_t p = pixels[i];
          result.alpha[i] = std::max(static_cast<uint8_t>(p >> 16),
              std::max(static_cast<uint8_t>(p >> 8), static_cast<uint8_t>(p)));
        }
      }
    }
  }
  cleanup();
  return result;
}
