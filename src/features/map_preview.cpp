// SPDX-License-Identifier: MIT
#include "features/map_preview.h"

#include <windows.h>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "core/log.h"
#include "core/mod.h"
#include "features/map_preview_pud.h"
#include "target/addresses.h"
#include "target/struct_offsets.h"

namespace {

using namespace game;

const Logger kLog{"preview"};

constexpr unsigned short kBuiltInMapIdFirst = 0x4c4;
constexpr unsigned short kBuiltInMapIdLast = 0x4df;

bool IsBuiltInScenarioId(unsigned short id) {
    return id >= kBuiltInMapIdFirst && id <= kBuiltInMapIdLast;
}

constexpr uint32_t kGlRgba = 0x1908;
constexpr uint32_t kGlUnsignedByte = 0x1401;
constexpr uint32_t kGlTextureMagFilter = 0x2800;
constexpr uint32_t kGlTextureMinFilter = 0x2801;
constexpr int kGlNearest = 0x2600;
constexpr int kGlLinear = 0x2601;

NkFillRectFn g_nkFillRect = nullptr;
NkDrawImageFn g_nkDrawImage = nullptr;
CreateGameTextureFn g_createGameTexture = nullptr;

LoadMapFileFn g_loadMapFile = nullptr;
GameFreeFn g_gameFree = nullptr;

constexpr uint32_t Rgba(int r, int g, int b, int a = 255) {
    return (uint32_t)(r & 0xff) | ((uint32_t)(g & 0xff) << 8) | ((uint32_t)(b & 0xff) << 16) |
           ((uint32_t)(a & 0xff) << 24);
}

struct Placement {
    float x;
    float y;
    float size;
};

constexpr int kMaxRects = 1700;

constexpr int kRenderMode = 1;

constexpr int kTexSize = 256;

constexpr int kTexFilter = 1;

constexpr int kTextureDim = 512;

bool ReadMapThroughGame(const std::string& path, unsigned short mapId,
                         std::vector<uint8_t>* out) {
    if (!g_loadMapFile || !g_gameFree) return false;

    void* buf = nullptr;
    unsigned int len = 0;

    const bool byId = path.empty();

    if (byId && !IsBuiltInScenarioId(mapId)) return false;
    if (!g_loadMapFile(byId ? mapId : 0, byId ? nullptr : path.c_str(), &buf, &len) || !buf) {
        return false;
    }

    bool ok = false;
    if (len >= 8 && len <= 64u * 1024u * 1024u) {
        out->assign(static_cast<const uint8_t*>(buf), static_cast<const uint8_t*>(buf) + len);
        ok = true;
    } else {
        kLog.Warn("game loader returned %u bytes for '%s' (id %u) -- refusing it", len,
            path.empty() ? "(by id)" : path.c_str(), (unsigned)mapId);
    }

    g_gameFree(buf, "map_preview.cpp", __LINE__, 0);
    return ok;
}

constexpr long kMaxPreviewFileBytes = 4 * 1024 * 1024;

bool ReadWholeFile(const std::string& path, std::vector<uint8_t>* out) {
    FILE* f = nullptr;
    if (fopen_s(&f, path.c_str(), "rb") != 0 || !f) return false;
    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (size <= 0 || size > kMaxPreviewFileBytes) {
        fclose(f);
        return false;
    }
    out->resize((size_t)size);
    size_t got = fread(out->data(), 1, (size_t)size, f);
    fclose(f);
    out->resize(got);
    return got != 0;
}

const std::string& GameDir() {
    static std::string dir = [] {
        char buf[MAX_PATH] = {0};
        GetModuleFileNameA(nullptr, buf, MAX_PATH);
        std::string s(buf);
        size_t slash = s.find_last_of("\\/");
        return slash == std::string::npos ? std::string(".") : s.substr(0, slash);
    }();
    return dir;
}

struct TilesetPaths {
    const char* folder;
    const char* stem;
};

const TilesetPaths kTilesets[4] = {
    {"Forest", "forest"},
    {"Iceland", "iceland"},
    {"Swamp", "swamp"},
    {"XSwamp", "xswamp"},
};

constexpr int kCv4RowEntries = 0x15;

constexpr int kSubGrid = 4;
constexpr int kSubPerTile = kSubGrid * kSubGrid;

struct Tileset {
    bool valid = false;
    std::vector<uint16_t> cv4;
    std::vector<uint32_t> tileColor;

    std::vector<uint32_t> subColor;
};

Tileset BuildTileset(int era) {
    Tileset ts;
    if (era < 0 || era > 3) return ts;

    const TilesetPaths& t = kTilesets[era];
    std::string base = GameDir() + "\\Data\\Art\\bgs\\" + t.folder + "\\" + t.stem;

    std::vector<uint8_t> cv4, vx4, vr4, ppl;
    if (!ReadWholeFile(base + ".cv4", &cv4) || !ReadWholeFile(base + ".vx4", &vx4) ||
        !ReadWholeFile(base + ".vr4", &vr4) || !ReadWholeFile(base + ".ppl", &ppl)) {
        kLog.Error("tileset %d (%s): missing art under %s", era, t.stem, base.c_str());
        return ts;
    }

    ts.cv4.resize(cv4.size() / 2);
    memcpy(ts.cv4.data(), cv4.data(), ts.cv4.size() * 2);

    const size_t paletteEntries = ppl.size() / 3;
    uint8_t pal[256][3] = {};
    for (size_t i = 0; i < paletteEntries && i < 256; ++i) {
        pal[i][0] = (uint8_t)(ppl[i * 3 + 0] << 2);
        pal[i][1] = (uint8_t)(ppl[i * 3 + 1] << 2);
        pal[i][2] = (uint8_t)(ppl[i * 3 + 2] << 2);
    }

    const size_t megatiles = vx4.size() / 32;
    const size_t minitiles = vr4.size() / 64;
    ts.tileColor.resize(megatiles);
    ts.subColor.resize(megatiles * kSubPerTile);
    for (size_t mt = 0; mt < megatiles; ++mt) {
        uint32_t tr = 0, tg = 0, tb = 0, tn = 0;
        for (int k = 0; k < kSubPerTile; ++k) {
            uint16_t entry;
            memcpy(&entry, &vx4[mt * 32 + k * 2], 2);

            size_t mi = entry >> 2;
            uint32_t r = 0, g = 0, b = 0, n = 0;
            if (mi < minitiles) {
                const uint8_t* px = &vr4[mi * 64];
                for (int p = 0; p < 64; ++p) {
                    r += pal[px[p]][0];
                    g += pal[px[p]][1];
                    b += pal[px[p]][2];
                    ++n;
                }
            }
            ts.subColor[mt * kSubPerTile + k] =
                n ? Rgba((int)(r / n), (int)(g / n), (int)(b / n)) : Rgba(0, 0, 0);
            tr += r;
            tg += g;
            tb += b;
            tn += n;
        }
        ts.tileColor[mt] = tn ? Rgba((int)(tr / tn), (int)(tg / tn), (int)(tb / tn)) : Rgba(0, 0, 0);
    }

    ts.valid = true;
    return ts;
}

const Tileset& GetTileset(int era) {
    static Tileset cache[4];
    static bool built[4] = {false, false, false, false};
    static Tileset empty;
    if (era < 0 || era > 3) return empty;
    if (!built[era]) {
        cache[era] = BuildTileset(era);
        built[era] = true;
        kLog.Info("tileset %d (%s): %s, %zu megatile colours", era, kTilesets[era].stem,
            cache[era].valid ? "loaded" : "FAILED", cache[era].tileColor.size());
    }
    return cache[era];
}

struct StartLocation {
    int x, y, owner;
};

const uint32_t kPlayerColors[8] = {
    Rgba(0xc8, 0x00, 0x00),
    Rgba(0x00, 0x00, 0xc8),
    Rgba(0x00, 0xc8, 0xc8),
    Rgba(0x80, 0x00, 0xc8),
    Rgba(0xff, 0x80, 0x00),
    Rgba(0x00, 0x00, 0x00),
    Rgba(0xff, 0xff, 0xff),
    Rgba(0xc8, 0xc8, 0x00),
};

constexpr uint32_t kGoldMineColor = Rgba(0xff, 0xd0, 0x20);
constexpr uint32_t kOilPatchColor = Rgba(0x20, 0x20, 0x20);

constexpr int kMineTiles = 3;
constexpr int kStartTiles = 4;

struct Preview {
    bool valid = false;
    std::string path;

    unsigned short mapId = 0;
    int budget = 0;
    int texSize = 0;

    int tilesetOverride = -1;
    int mapW = 0, mapH = 0;

    int factor = 1;
    int width = 0, height = 0;
    std::vector<uint32_t> color;
    std::vector<StartLocation> starts;
    std::vector<StartLocation> mines;

    int texW = 0, texH = 0;
    std::vector<uint32_t> texels;
    bool uploaded = false;
};

Preview g_preview;

Preview BuildPreview(const char* path, unsigned short mapId, int maxRects, int texSize,
                      int tilesetOverride) {
    Preview p;
    p.path = path;
    p.mapId = mapId;
    p.budget = maxRects;
    p.texSize = texSize;
    p.tilesetOverride = tilesetOverride;

    std::vector<uint8_t> pud;
    if (!ReadWholeFile(path, &pud) && !ReadMapThroughGame(path, mapId, &pud)) {
        kLog.Warn("cannot open '%s' as a loose file or through the game's loader", path);
        return p;
    }
    map_preview_pud::PudView view;
    switch (map_preview_pud::Parse(pud.data(), pud.size(), &view)) {
    case map_preview_pud::ParseFault::Ok:
        break;
    case map_preview_pud::ParseFault::NotPud:
        kLog.Warn("'%s' is not a PUD", path);
        return p;
    case map_preview_pud::ParseFault::MissingSection:
        kLog.Warn("'%s' is missing DIM/ERA/MTXM", path);
        return p;
    case map_preview_pud::ParseFault::BadDims:
        kLog.Warn("'%s' has implausible dimensions %ux%u", path, view.w, view.h);
        return p;
    case map_preview_pud::ParseFault::ShortMtxm:
        kLog.Warn("'%s' MTXM is shorter than the %u bytes %ux%u needs", path, (unsigned)view.w * view.h * 2,
            view.w, view.h);
        return p;
    }
    const uint16_t w = view.w, h = view.h;
    const uint8_t* const mtxm = view.mtxm;
    uint16_t eraIndex = view.era;

    if (tilesetOverride >= 0 && tilesetOverride <= 3) eraIndex = (uint16_t)tilesetOverride;

    const Tileset& ts = GetTileset(eraIndex);
    if (!ts.valid) return p;

    p.mapW = w;
    p.mapH = h;

    std::vector<uint16_t> megatile((size_t)w * h, 0);
    for (int ty = 0; ty < h; ++ty) {
        for (int tx = 0; tx < w; ++tx) {
            uint16_t raw;
            memcpy(&raw, mtxm + ((size_t)ty * w + tx) * 2, 2);
            size_t idx = (size_t)(raw >> 4) * kCv4RowEntries + (raw & 0x0f);
            uint16_t mt = idx < ts.cv4.size() ? ts.cv4[idx] : 0;
            if (eraIndex == 3 && mt == 0) mt = 0x153;
            megatile[(size_t)ty * w + tx] = mt;
        }
    }

    auto subColorAt = [&](int tx, int ty, int sub) -> uint32_t {
        const size_t mt = megatile[(size_t)ty * w + tx];
        const size_t k = mt * kSubPerTile + sub;
        return k < ts.subColor.size() ? ts.subColor[k] : Rgba(0, 0, 0);
    };
    auto tileColorAt = [&](int tx, int ty) -> uint32_t {
        const size_t mt = megatile[(size_t)ty * w + tx];
        return mt < ts.tileColor.size() ? ts.tileColor[mt] : Rgba(0, 0, 0);
    };

    using RawUnit = map_preview_pud::Marker;
    std::vector<RawUnit> units;
    map_preview_pud::CollectMarkers(view, &units);

    const int kBorderRects = 4;
    int terrainBudget = maxRects - (int)units.size() - kBorderRects;
    if (terrainBudget < 64) terrainBudget = 64;

    {
        int gw = w, gh = h;
        while (gw * gh > terrainBudget && gw > 8 && gh > 8) {

            if (gw >= gh) --gw;
            else --gh;
        }
        p.width = gw;
        p.height = gh;
    }
    p.factor = (w + p.width - 1) / p.width;

    p.color.assign((size_t)p.width * p.height, Rgba(0, 0, 0));
    for (int cy = 0; cy < p.height; ++cy) {

        const int y0 = (int)((int64_t)cy * h / p.height);
        const int y1 = (int)((int64_t)(cy + 1) * h / p.height);
        for (int cx = 0; cx < p.width; ++cx) {
            const int x0 = (int)((int64_t)cx * w / p.width);
            const int x1 = (int)((int64_t)(cx + 1) * w / p.width);
            uint32_t r = 0, g = 0, b = 0, n = 0;
            for (int sy = y0; sy < (y1 > y0 ? y1 : y0 + 1) && sy < h; ++sy) {
                for (int sx = x0; sx < (x1 > x0 ? x1 : x0 + 1) && sx < w; ++sx) {
                    uint32_t c = tileColorAt(sx, sy);
                    r += c & 0xff;
                    g += (c >> 8) & 0xff;
                    b += (c >> 16) & 0xff;
                    ++n;
                }
            }
            if (n) p.color[(size_t)cy * p.width + cx] = Rgba((int)(r / n), (int)(g / n), (int)(b / n));
        }
    }

    for (const RawUnit& u : units) {
        const int cx = (int)((int64_t)u.x * p.width / w);
        const int cy = (int)((int64_t)u.y * p.height / h);
        if (u.isStart) p.starts.push_back({cx, cy, u.owner});
        else p.mines.push_back({cx, cy, u.isGold ? 1 : 0});
    }

    {
        const int srcW = w * kSubGrid;
        const int srcH = h * kSubGrid;
        const int srcMax = srcW > srcH ? srcW : srcH;

        p.texW = (int)((int64_t)texSize * srcW / srcMax);
        p.texH = (int)((int64_t)texSize * srcH / srcMax);
        if (p.texW < 1) p.texW = 1;
        if (p.texH < 1) p.texH = 1;

        p.texels.assign((size_t)p.texW * p.texH, Rgba(0, 0, 0));
        for (int py = 0; py < p.texH; ++py) {
            const int sy0 = (int)((int64_t)py * srcH / p.texH);
            int sy1 = (int)((int64_t)(py + 1) * srcH / p.texH);
            if (sy1 <= sy0) sy1 = sy0 + 1;
            for (int px = 0; px < p.texW; ++px) {
                const int sx0 = (int)((int64_t)px * srcW / p.texW);
                int sx1 = (int)((int64_t)(px + 1) * srcW / p.texW);
                if (sx1 <= sx0) sx1 = sx0 + 1;
                uint32_t r = 0, g = 0, b = 0, n = 0;
                for (int sy = sy0; sy < sy1 && sy < srcH; ++sy) {
                    for (int sx = sx0; sx < sx1 && sx < srcW; ++sx) {
                        const uint32_t c = subColorAt(sx / kSubGrid, sy / kSubGrid,
                                                      (sy % kSubGrid) * kSubGrid + sx % kSubGrid);
                        r += c & 0xff;
                        g += (c >> 8) & 0xff;
                        b += (c >> 16) & 0xff;
                        ++n;
                    }
                }
                if (n) {
                    p.texels[(size_t)py * p.texW + px] =
                        Rgba((int)(r / n), (int)(g / n), (int)(b / n));
                }
            }
        }

        const int texelsPerTile = p.texW / (w > 0 ? w : 1);
        const int mineRaw = texelsPerTile * kMineTiles;
        const int startRaw = texelsPerTile * kStartTiles;
        const int mineSize = mineRaw < 2 ? 2 : mineRaw;
        const int startSize = startRaw < 3 ? 3 : startRaw;

        auto stamp = [&](int ux, int uy, int size, uint32_t color) {
            const int cx = (int)((int64_t)ux * p.texW / w);
            const int cy = (int)((int64_t)uy * p.texH / h);
            for (int dy = 0; dy < size; ++dy) {
                const int ty = cy + dy;
                if (ty < 0 || ty >= p.texH) continue;
                for (int dx = 0; dx < size; ++dx) {
                    const int tx = cx + dx;
                    if (tx < 0 || tx >= p.texW) continue;
                    p.texels[(size_t)ty * p.texW + tx] = color;
                }
            }
        };
        for (const RawUnit& u : units) {
            if (u.isStart) continue;
            stamp(u.x, u.y, mineSize, u.isGold ? kGoldMineColor : kOilPatchColor);
        }

        for (const RawUnit& u : units) {
            if (!u.isStart) continue;
            stamp(u.x, u.y, startSize, kPlayerColors[u.owner & 7]);
        }
    }

    p.valid = true;
    kLog.Info("built '%s': map %dx%d tileset=%u -> rect grid %dx%d = %d cells (~%d tiles/cell), "
        "texture %dx%d, starts=%zu mines=%zu, budget=%d (terrain %d)",
        path, w, h, eraIndex, p.width, p.height, p.width * p.height, p.factor, p.texW, p.texH,
        p.starts.size(), p.mines.size(), maxRects, terrainBudget);
    return p;
}

constexpr uint32_t kBorderColor = Rgba(0x18, 0x14, 0x0c);

int g_rectsLeft = 0;

void FillRect(void* canvas, float x, float y, float w, float h, uint32_t color) {
    if (g_rectsLeft <= 0) return;
    --g_rectsLeft;
    g_nkFillRect(canvas, x, y, w, h, 0.0f, color);
}

void DrawBorder(void* canvas, float boxX, float boxY, float boxW, float boxH, float b) {
    FillRect(canvas, boxX - b, boxY - b, boxW + 2 * b, b, kBorderColor);
    FillRect(canvas, boxX - b, boxY + boxH, boxW + 2 * b, b, kBorderColor);
    FillRect(canvas, boxX - b, boxY, b, boxH, kBorderColor);
    FillRect(canvas, boxX + boxW, boxY, b, boxH, kBorderColor);
}

GameTexture* g_texture = nullptr;
bool g_textureFailed = false;
int g_appliedFilter = -1;

template <typename T>
T GlProc(game::Obj<T> slot) {
    return *slot.Get();
}

bool EnsureTexture() {
    if (g_texture) return true;
    if (g_textureFailed) return false;
    g_textureFailed = true;

    if (!g_createGameTexture) {
        kLog.Error("texture path disabled: CreateGameTexture unresolved");
        return false;
    }
    if (!GlProc(kGlTextureSubImage2DSlot) ||
        !GlProc(kGlTextureParameteriSlot)) {

        kLog.Error("texture path disabled: GL entry points are null (renderer not initialized?)");
        return false;
    }

    std::vector<uint8_t> blank((size_t)kTextureDim * kTextureDim * 4, 0);
    GameTexture* tex = nullptr;
    g_createGameTexture(&tex, kTextureDim, kTextureDim, blank.data(), kGameTextureFormatRgba8,
                        kTexFilter ? kGameTextureFilterLinear : kGameTextureFilterNearest);
    if (!tex || tex->glName == 0 || tex->w != kTextureDim || tex->h != kTextureDim) {
        kLog.Error("texture path disabled: CreateGameTexture returned %p (name=%u %dx%d)", tex,
            tex ? tex->glName : 0u, tex ? tex->w : 0, tex ? tex->h : 0);
        return false;
    }

    g_texture = tex;
    g_appliedFilter = kTexFilter ? 1 : 0;
    g_textureFailed = false;
    kLog.Info("texture created: %dx%d GL name=%u format=%d filter=%d (GameTexture at %p)", tex->w, tex->h,
        tex->glName, tex->format, tex->filter, tex);
    return true;
}

void ApplyFilter() {
    const int want = kTexFilter ? 1 : 0;
    if (!g_texture || want == g_appliedFilter) return;
    g_appliedFilter = want;
    const int mode = want ? kGlLinear : kGlNearest;
    auto param = GlProc(kGlTextureParameteriSlot);
    param(g_texture->glName, kGlTextureMinFilter, mode);
    param(g_texture->glName, kGlTextureMagFilter, mode);
    kLog.Info("texture filter -> %s", want ? "GL_LINEAR" : "GL_NEAREST");
}

void UploadPreview(Preview& p) {
    if (p.uploaded || p.texels.empty()) return;
    GlProc(kGlTextureSubImage2DSlot)(
        g_texture->glName, 0, 0, 0, p.texW, p.texH, kGlRgba, kGlUnsignedByte, p.texels.data());
    p.uploaded = true;
    kLog.Info("uploaded %dx%d texels to GL texture %u", p.texW, p.texH, g_texture->glName);
}

void DrawPreviewTexture(void* canvas, const Preview& p, float boxX, float boxY, float boxW,
                        float boxH) {
    DrawBorder(canvas, boxX, boxY, boxW, boxH, 4.0f);
    if (g_rectsLeft <= 0) return;
    --g_rectsLeft;

    NkImage img;
    img.handle = g_texture;

    img.w = (uint16_t)kTextureDim;
    img.h = (uint16_t)kTextureDim;
    img.region[0] = 0;
    img.region[1] = 0;
    img.region[2] = (uint16_t)p.texW;
    img.region[3] = (uint16_t)p.texH;
    g_nkDrawImage(canvas, boxX, boxY, boxW, boxH, &img, Rgba(255, 255, 255));
}

void DrawPreviewRects(void* canvas, const Preview& p, float boxX, float boxY, float cell) {
    const float boxW = cell * p.width;
    const float boxH = cell * p.height;

    DrawBorder(canvas, boxX, boxY, boxW, boxH, cell < 3.0f ? 2.0f : 4.0f);

    const int markerBudget = (int)(p.starts.size() + p.mines.size());

    int lastRow = 0;
    for (int y = 0; y < p.height && g_rectsLeft > markerBudget; ++y) {
        lastRow = y + 1;
        int runStart = 0;
        uint32_t runColor = p.color[(size_t)y * p.width];
        for (int x = 1; x <= p.width; ++x) {
            uint32_t here = x < p.width ? p.color[(size_t)y * p.width + x] : ~runColor;
            if (here == runColor) continue;
            FillRect(canvas, boxX + runStart * cell, boxY + y * cell, (x - runStart) * cell, cell,
                     runColor);
            runStart = x;
            runColor = here;
        }
    }

    if (lastRow < p.height) {
        static std::string warnedFor;
        if (warnedFor != p.path) {
            warnedFor = p.path;
            kLog.Warn("TRUNCATED: only %d of %d rows drawn -- raise maxrects (now %d) or accept a "
                "coarser grid", lastRow, p.height, kMaxRects);
        }
    }

    const float pixelsPerTile = cell / (float)(p.factor > 0 ? p.factor : 1);
    const float mineRaw = pixelsPerTile * (float)kMineTiles;
    const float markerCell = mineRaw < 2.0f ? 2.0f : mineRaw;
    for (const StartLocation& m : p.mines) {
        FillRect(canvas, boxX + m.x * cell, boxY + m.y * cell, markerCell, markerCell,
                 m.owner ? kGoldMineColor : kOilPatchColor);
    }

    const float startRaw = pixelsPerTile * (float)kStartTiles;
    const float startSize = startRaw < 3.0f ? 3.0f : startRaw;

    for (const StartLocation& s : p.starts) {
        FillRect(canvas, boxX + s.x * cell, boxY + s.y * cell, startSize, startSize,
                 kPlayerColors[s.owner & 7]);
    }
}

struct DrawContext {
    void* ctx = nullptr;
    void* win = nullptr;
    void* canvas = nullptr;
    float uiW = 0.0f, uiH = 0.0f;
    const char* path = nullptr;
    unsigned short mapId = 0;
    int tilesetOverride = -1;
    const char* stopReason = nullptr;
};

DrawContext ProbeWindow() {
    DrawContext d;
    d.ctx = *kNkContextMenus.Get();
    if (!d.ctx) {
        d.stopReason = "g_pNkContext is null";
        return d;
    }
    d.win = *reinterpret_cast<void**>(reinterpret_cast<uintptr_t>(d.ctx) + nk::kCtxCurrentWindow);
    if (!d.win) {
        d.stopReason = "ctx->current is null (no window is open at this moment)";
        return d;
    }

    const float* bounds =
        reinterpret_cast<const float*>(reinterpret_cast<uintptr_t>(d.win) + nk::kWindowBounds);
    d.uiW = bounds[2];
    d.uiH = bounds[3];
    if (!(d.uiW > 100.0f && d.uiW < 20000.0f && d.uiH > 100.0f && d.uiH < 20000.0f)) {
        d.stopReason = "win->bounds implausible";
        return d;
    }

    d.canvas = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(d.win) + nk::kWindowBuffer);
    return d;
}

void DrawAt(const DrawContext& d, const Placement& place) {
    g_rectsLeft = kMaxRects;

    if (d.stopReason) return;

    if (g_preview.path != d.path || g_preview.mapId != d.mapId ||
        g_preview.budget != kMaxRects || g_preview.texSize != kTexSize ||
        g_preview.tilesetOverride != d.tilesetOverride) {
        g_preview = BuildPreview(d.path, d.mapId, kMaxRects, kTexSize, d.tilesetOverride);
    }
    if (!g_preview.valid) return;

    bool useTexture = kRenderMode != 0 && g_nkDrawImage != nullptr && EnsureTexture();
    if (useTexture) {
        ApplyFilter();
        UploadPreview(g_preview);
    }

    float cell = (float)(int)(place.size / g_preview.width);
    if (cell < 1.0f) cell = 1.0f;

    float boxW, boxH;
    if (useTexture) {

        boxW = place.size;
        boxH = place.size;
        if (g_preview.texW >= g_preview.texH) {
            boxH = place.size * (float)g_preview.texH / (float)g_preview.texW;
        } else {
            boxW = place.size * (float)g_preview.texW / (float)g_preview.texH;
        }
    } else {
        boxW = cell * g_preview.width;
        boxH = cell * g_preview.height;
    }

    float boxX = place.x;
    if (boxX < 0.0f) boxX = (500.0f - boxW) * 0.5f;
    const float boxY = place.y;

    if (useTexture) DrawPreviewTexture(d.canvas, g_preview, boxX, boxY, boxW, boxH);
    else DrawPreviewRects(d.canvas, g_preview, boxX, boxY, cell);
}

}  // namespace

void DrawMapPreviewAt(float x, float y, float size, const char* path, unsigned short mapId,
                      int tilesetOverride) {
    DrawContext d = ProbeWindow();
    if (d.stopReason || !d.canvas) return;
    d.path = path ? path : "";
    d.mapId = mapId;
    d.tilesetOverride = tilesetOverride;

    if (!d.path[0] && !IsBuiltInScenarioId(d.mapId)) return;
    DrawAt(d, Placement{x, y, size});
}

namespace {

void ResolveAndRegister() {
    g_nkFillRect = kNkFillRect.Get();
    g_nkDrawImage = kNkDrawImage.Get();

    g_createGameTexture = kCreateGameTexture.Get();
    g_loadMapFile = kLoadMapFile.Get();
    g_gameFree = kGameFree.Get();

    kLog.Info("ready");
}

class MapPreviewMod : public IMod {
public:
    const char* Name() const override { return "preview"; }
    bool Install() override {
        ResolveAndRegister();
        return true;
    }
};

}  // namespace

MOD_REGISTER(MapPreviewMod)
