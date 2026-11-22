// UI glyphs for the Linux build (see linux/shim/jni.h): what MDActivity.renderGlyphs does on Android, with
// stb_truetype. Fonts: <exe dir>/fonts (the AppImage bundles DejaVu Sans and Luckiest Guy there), then the
// system's sans-serif through fontconfig (fc-match), then the usual DejaVu paths.
#define STB_TRUETYPE_IMPLEMENTATION
#include "stb_truetype.h"
#include "app/sys.h"
#include <jni.h>
#include <cmath>
#include <cstdio>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace {

struct Font {
    std::vector<uint8_t> data;
    stbtt_fontinfo info;
};

// Loaded fonts by path (null: the file couldn't be read); kept for the life of the process.
Font* load_font(const std::string& path) {
    static std::map<std::string, std::unique_ptr<Font>> cache;
    auto it = cache.find(path);
    if (it != cache.end()) return it->second.get();
    std::unique_ptr<Font> f(new Font);
    if (path.empty() || !read_file(from_utf8(path), f->data) || f->data.empty() ||
        !stbtt_InitFont(&f->info, f->data.data(), stbtt_GetFontOffsetForIndex(f->data.data(), 0)))
        f.reset();
    Font* r = f.get();
    cache[path] = std::move(f);
    return r;
}

std::string fc_match(const char* pattern) {
    std::string cmd = std::string("fc-match -f '%{file}' '") + pattern + "' 2>/dev/null";
    std::string out;
    if (FILE* p = popen(cmd.c_str(), "r")) {
        char buf[512];
        size_t n;
        while ((n = fread(buf, 1, sizeof buf, p)) > 0) out.append(buf, n);
        pclose(p);
    }
    return out;
}

// The fonts to try for a style, first to last: a glyph comes from the first one that has it.
std::vector<Font*> chain(bool bold, bool title) {
    const std::string dir = to_utf8(exe_dir()) + "/fonts/";
    std::vector<std::string> paths;
    if (title) paths.push_back(dir + "LuckiestGuy-Regular.ttf");
    if (bold) {
        paths.push_back(dir + "DejaVuSans-Bold.ttf");
        paths.push_back(fc_match("sans-serif:bold"));
        paths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf");
        paths.push_back("/usr/share/fonts/TTF/DejaVuSans-Bold.ttf");
    }
    paths.push_back(dir + "DejaVuSans.ttf");
    paths.push_back(fc_match("sans-serif"));
    paths.push_back("/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf");
    paths.push_back("/usr/share/fonts/TTF/DejaVuSans.ttf");
    std::vector<Font*> fonts;
    for (const std::string& p : paths)
        if (Font* f = load_font(p)) {
            bool dup = false;
            for (Font* g : fonts) dup = dup || g == f;
            if (!dup) fonts.push_back(f);
        }
    return fonts;
}

// (the display font's lower case letters are odd shapes: its capitals instead, as on Android)
uint32_t upper(uint32_t c) {
    if (c >= 'a' && c <= 'z') return c - 32;
    if (c >= 0xE0 && c <= 0xFE && c != 0xF7) return c - 32;
    if (c == 0xFF) return 0x178;
    return c;
}

}  // namespace

std::vector<uint8_t> linux_render_glyphs(const std::vector<int32_t>& cps, float px, bool bold, bool title,
                                         std::vector<int32_t>& meta) {
    meta.assign(2 + cps.size() * 5, 0);
    std::vector<uint8_t> bits;
    const std::vector<Font*> fonts = chain(bold, title);
    if (fonts.empty()) {
        fprintf(stderr, "MickeyDonald: no font found (install fontconfig and a sans-serif font, e.g. DejaVu)\n");
        for (size_t i = 0; i < cps.size(); i++) meta[2 + i * 5] = -1;
        return bits;
    }
    {
        const stbtt_fontinfo& p = fonts[0]->info;
        const float s = stbtt_ScaleForMappingEmToPixels(&p, px);
        int asc, desc, gap;
        stbtt_GetFontVMetrics(&p, &asc, &desc, &gap);
        meta[0] = (int)std::ceil(asc * s);
        meta[1] = (int)std::ceil((asc - desc + gap) * s);
    }
    for (size_t i = 0; i < cps.size(); i++) {
        int32_t* m = &meta[2 + i * 5];
        const uint32_t cp = title && cps[i] < 0x250 ? upper((uint32_t)cps[i]) : (uint32_t)cps[i];
        const stbtt_fontinfo* f = nullptr;
        for (Font* c : fonts)
            if (cp == ' ' || stbtt_FindGlyphIndex(&c->info, (int)cp)) { f = &c->info; break; }
        if (!f) { m[0] = -1; continue; }
        const float s = stbtt_ScaleForMappingEmToPixels(f, px);
        const int glyph = stbtt_FindGlyphIndex(f, (int)cp);
        int adv, lsb;
        stbtt_GetGlyphHMetrics(f, glyph, &adv, &lsb);
        m[0] = (int)std::lround(adv * s * 64);
        int x0, y0, x1, y1;
        stbtt_GetGlyphBitmapBox(f, glyph, s, s, &x0, &y0, &x1, &y1);
        if (x1 <= x0 || y1 <= y0) continue;   // (no ink: a space)
        const int w = x1 - x0 + 2, h = y1 - y0 + 2;   // a pixel of room for anti-aliasing on each side
        const size_t off = bits.size();
        bits.resize(off + (size_t)w * h, 0);
        stbtt_MakeGlyphBitmap(f, &bits[off + w + 1], w - 2, h - 2, w, s, s, glyph);
        m[1] = x0 - 1;
        m[2] = y0 - 1;
        m[3] = w;
        m[4] = h;
    }
    return bits;
}
