/* Scale only the already bundled Ubuntu numeral glyphs. No external font or
 * generated artwork is needed. LVGL consumes each returned bitmap immediately. */
struct home_font_source_t { const lv_font_t *font; uint8_t num, den; };
#if defined(LILYGO_WATCH_ULTRA)
static home_font_source_t home_large_source = { &Ubuntu_144px, 3, 4 };
#else
static home_font_source_t home_large_source = { &Ubuntu_72px, 3, 4 };
#endif
static home_font_source_t home_small_source = { &Ubuntu_72px, 7, 8 };
static home_font_source_t home_landscape_source = { &Ubuntu_72px, 5, 6 };
static lv_font_t home_large_font, home_small_font, home_landscape_font;
#if defined(LILYGO_WATCH_ULTRA)
static uint8_t home_glyph_pixels[120 * 150];
#else
static uint8_t home_glyph_pixels[64 * 90];
#endif

static bool home_glyph_descriptor(const lv_font_t *font, lv_font_glyph_dsc_t *out, uint32_t letter, uint32_t next) {
    const home_font_source_t *s = (const home_font_source_t *)font->dsc;
    if (!lv_font_get_glyph_dsc(s->font, out, letter, next)) return false;
    out->adv_w = (out->adv_w * s->num + s->den / 2) / s->den;
    out->box_w = out->box_w * s->num / s->den;
    out->box_h = out->box_h * s->num / s->den;
    out->ofs_x = out->ofs_x * s->num / s->den;
    out->ofs_y = out->ofs_y * s->num / s->den;
    out->bpp = 8;
    return true;
}

static const uint8_t *home_glyph_bitmap(const lv_font_t *font, uint32_t letter) {
    const home_font_source_t *s = (const home_font_source_t *)font->dsc;
    lv_font_glyph_dsc_t original, scaled;
    if (!lv_font_get_glyph_dsc(s->font, &original, letter, 0) ||
        !home_glyph_descriptor(font, &scaled, letter, 0) ||
        (size_t)scaled.box_w * scaled.box_h > sizeof(home_glyph_pixels)) return NULL;
    const uint8_t *pixels = lv_font_get_glyph_bitmap(s->font, letter);
    if (!pixels) return NULL;
    const unsigned mask = (1u << original.bpp) - 1;
    for (unsigned y = 0; y < scaled.box_h; ++y) {
        for (unsigned x = 0; x < scaled.box_w; ++x) {
            const unsigned index = ((y * s->den / s->num) * original.box_w + x * s->den / s->num) * original.bpp;
            const unsigned sample = (pixels[index / 8] >> (8 - original.bpp - index % 8)) & mask;
            home_glyph_pixels[y * scaled.box_w + x] = sample * 255 / mask;
        }
    }
    return home_glyph_pixels;
}

static void home_clock_fonts_init() {
    home_large_font = *home_large_source.font;
    home_small_font = Ubuntu_72px;
    home_landscape_font = Ubuntu_72px;
    lv_font_t *fonts[] = { &home_large_font, &home_small_font, &home_landscape_font };
    home_font_source_t *sources[] = { &home_large_source, &home_small_source, &home_landscape_source };
    for (int i = 0; i < 3; ++i) {
        fonts[i]->dsc = sources[i];
        fonts[i]->get_glyph_dsc = home_glyph_descriptor;
        fonts[i]->get_glyph_bitmap = home_glyph_bitmap;
        fonts[i]->line_height = sources[i]->font->line_height * sources[i]->num / sources[i]->den;
        fonts[i]->base_line = sources[i]->font->base_line * sources[i]->num / sources[i]->den;
    }
}
