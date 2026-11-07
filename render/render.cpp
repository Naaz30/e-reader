#include "render.h"

#include <string.h>

#include "esp_heap_caps.h"

#include "esp_log.h"
#include "font.h"
#include "stb_truetype.h"

static const char *TAG = "render";


static void set_pixel_black(uint8_t *fb, size_t stride, int x, int y)
{
    uint8_t &b = fb[(size_t)y * stride + x / 2];
    if (x & 1) b &= 0xF0;
    else       b &= 0x0F;
}

static void map_logical(uint16_t pw, uint16_t ph, int lx, int ly, int &x, int &y)
{
    x = lx;
    y = ly;
    if (PORTRAIT) {
        x = PORTRAIT_FLIP ? (pw - 1 - ly) : ly;
        y = PORTRAIT_FLIP ? lx : (ph - 1 - lx);
    }
}

static void set_logical_pixel(uint8_t *fb, uint16_t pw, uint16_t ph, int lx, int ly)
{
    int x, y;
    map_logical(pw, ph, lx, ly, x, y);
    set_pixel_black(fb, (pw + 1) / 2, x, y);
}

static void put_gray(uint8_t *fb, uint16_t pw, uint16_t ph, int lx, int ly, uint8_t gray)
{
    int x, y;
    map_logical(pw, ph, lx, ly, x, y);
    uint8_t &b = fb[(size_t)y * ((pw + 1) / 2) + x / 2];
    if (x & 1) {
        if ((b & 0x0F) > gray) b = (b & 0xF0) | gray;
    } else {
        if ((b >> 4) > gray) b = (b & 0x0F) | (uint8_t)(gray << 4);
    }
}
static bool is_ws(char c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

static uint32_t utf8_next(const char *s, size_t len, size_t &i)
{
    const uint8_t c = (uint8_t)s[i++];
    if (c < 0x80) return c;
    int extra;
    uint32_t cp;
    if (c >= 0xF0)      { extra = 3; cp = c & 0x07; }
    else if (c >= 0xE0) { extra = 2; cp = c & 0x0F; }
    else if (c >= 0xC0) { extra = 1; cp = c & 0x1F; }
    else return 0xFFFD;  // stray continuation byte
    while (extra-- > 0 && i < len && ((uint8_t)s[i] & 0xC0) == 0x80)
        cp = (cp << 6) | ((uint8_t)s[i++] & 0x3F);
    return cp;
}

static float advance_of(const stbtt_fontinfo &f, float scale, uint32_t cp, uint32_t next)
{
    int adv, lsb;
    stbtt_GetCodepointHMetrics(&f, cp, &adv, &lsb);
    float a = adv * scale;
    if (next) a += stbtt_GetCodepointKernAdvance(&f, cp, next) * scale;
    return a;
}

static float measure_word(const stbtt_fontinfo &f, float scale, const char *text,
                          size_t len, size_t i, size_t &j)
{
    float w = 0;
    size_t k = i;
    while (k < len && !is_ws(text[k])) {
        size_t kk = k;
        const uint32_t cp = utf8_next(text, len, kk);
        uint32_t nxt = 0;
        if (kk < len && !is_ws(text[kk])) {
            size_t pk = kk;
            nxt = utf8_next(text, len, pk);
        }
        w += advance_of(f, scale, cp, nxt);
        k = kk;
    }
    j = k;
    return w;
}

static uint8_t page_gbuf[GLYPH_BUF];

static void draw_glyph(uint8_t *fb, uint16_t pw, uint16_t ph, int lw, int lh,
                       const stbtt_fontinfo &f, float scale, uint32_t cp,
                       float pen_x, int baseline, uint8_t *gbuf, size_t gcap)
{
    int x0, y0, x1, y1;
    stbtt_GetCodepointBitmapBox(&f, cp, scale, scale, &x0, &y0, &x1, &y1);
    const int gw = x1 - x0, gh = y1 - y0;
    if (gw <= 0 || gh <= 0 || (size_t)(gw * gh) > gcap) return;

    memset(gbuf, 0, (size_t)gw * gh);
    stbtt_MakeCodepointBitmap(&f, gbuf, gw, gh, gw, scale, scale, cp);

    const int gx = (int)(pen_x + 0.5f) + x0;
    const int gy = baseline + y0;
    for (int yy = 0; yy < gh; yy++) {
        for (int xx = 0; xx < gw; xx++) {
            const uint8_t cov = gbuf[yy * gw + xx];  
            if (!cov) continue;
            const int lx = gx + xx, ly = gy + yy;
            if (lx < 0 || lx >= lw || ly < 0 || ly >= lh) continue;
            const uint8_t gray = (uint8_t)(15 - (cov * 15 + 127) / 255);
            put_gray(fb, pw, ph, lx, ly, gray);
        }
    }
}

size_t render_page(uint8_t *fb, uint16_t pw, uint16_t ph,
                   const char *text, size_t len, size_t start)
{
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_data(), stbtt_GetFontOffsetForIndex(font_data(), 0))) {
        ESP_LOGE(TAG, "could not read the embedded font");
        return start;
    }
    const float scale = stbtt_ScaleForPixelHeight(&font, FONT_PX);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &linegap);

    const int lw = PORTRAIT ? ph : pw;  
    const int lh = PORTRAIT ? pw : ph;
    const float left = MARGIN_X;
    const float right = lw - MARGIN_X;
    const float bottom = lh - MARGIN_BOTTOM;
    const float line_h = FONT_PX * LINE_SPACING;
    const float space_w = advance_of(font, scale, ' ', 0);

    float baseline = MARGIN_TOP + ascent * scale;
    float pen_x = left;
    size_t i = start;

    while (i < len && is_ws(text[i])) i++;  

    while (i < len) {
        if (text[i] == '\n' || text[i] == '\r') {
            int newlines = 0;
            size_t j = i;
            while (j < len && is_ws(text[j])) {
                if (text[j] == '\n') newlines++;
                j++;
            }
            i = j;
            if (newlines >= 2) {
                baseline += line_h * 1.5f;  
                pen_x = left;
                if (baseline - descent * scale > bottom) return i;
            } else if (pen_x > left) {
                pen_x += space_w;
            }
            continue;
        }
        if (text[i] == ' ' || text[i] == '\t') {
            if (pen_x > left) pen_x += space_w;
            i++;
            continue;
        }

       
        size_t j;
        const float w = measure_word(font, scale, text, len, i, j);
        if (pen_x + w > right && pen_x > left) {
            baseline += line_h;
            pen_x = left;
            if (baseline - descent * scale > bottom) return i; 
        }
        size_t k = i;
        while (k < j) {
            size_t kk = k;
            const uint32_t cp = utf8_next(text, len, kk);
            uint32_t nxt = 0;
            if (kk < j) {
                size_t pk = kk;
                nxt = utf8_next(text, len, pk);
            }
            draw_glyph(fb, pw, ph, lw, lh, font, scale, cp, pen_x, (int)(baseline + 0.5f),
                       page_gbuf, sizeof(page_gbuf));
            pen_x += advance_of(font, scale, cp, nxt);
            k = kk;
        }
        i = j;
    }
    return len;
}


static float line_width(const stbtt_fontinfo &f, float scale, const char *text, size_t len)
{
    float w = 0;
    size_t i = 0;
    while (i < len) {
        const uint32_t cp = utf8_next(text, len, i);
        uint32_t nxt = 0;
        if (i < len) {
            size_t pk = i;
            nxt = utf8_next(text, len, pk);
        }
        w += advance_of(f, scale, cp, nxt);
    }
    return w;
}

void render_splash(uint8_t *fb, uint16_t pw, uint16_t ph, const char *text)
{
    stbtt_fontinfo font;
    if (!stbtt_InitFont(&font, font_script_data(),
                        stbtt_GetFontOffsetForIndex(font_script_data(), 0))) {
        ESP_LOGE(TAG, "could not read the handwriting font");
        return;
    }
    const size_t len = strlen(text);
    const int lw = PORTRAIT ? ph : pw; 
    const int lh = PORTRAIT ? pw : ph;

    const float w100 = line_width(font, stbtt_ScaleForPixelHeight(&font, 100.0f), text, len);
    if (w100 <= 0) return;
    float px = 100.0f * (lw * 0.80f) / w100;
    if (px > SPLASH_MAX_PX) px = SPLASH_MAX_PX;

    const float scale = stbtt_ScaleForPixelHeight(&font, px);
    int ascent, descent, linegap;
    stbtt_GetFontVMetrics(&font, &ascent, &descent, &linegap);

    float pen_x = (lw - line_width(font, scale, text, len)) / 2.0f;     
    const int baseline = (int)(lh / 2.0f + (ascent + descent) * scale / 2.0f + 0.5f); 

    const size_t gcap = (size_t)SPLASH_GLYPH_SIDE * SPLASH_GLYPH_SIDE;
    uint8_t *gbuf = (uint8_t *)heap_caps_malloc(gcap, MALLOC_CAP_SPIRAM);
    if (!gbuf) {
        ESP_LOGE(TAG, "out of memory for the greeting");
        return;
    }
    size_t i = 0;
    while (i < len) {
        const uint32_t cp = utf8_next(text, len, i);
        uint32_t nxt = 0;
        if (i < len) {
            size_t pk = i;
            nxt = utf8_next(text, len, pk);
        }
        draw_glyph(fb, pw, ph, lw, lh, font, scale, cp, pen_x, baseline, gbuf, gcap);
        pen_x += advance_of(font, scale, cp, nxt);
    }
    free(gbuf);
}
