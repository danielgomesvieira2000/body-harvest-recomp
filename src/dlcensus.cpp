// Display-list census and HUD inspector feed. See include/bh/dlcensus.h.

#include "bh/dlcensus.h"
#include "bh/gbi_f3dex.h"
#include "bh/hudid.h"
#include "bh/inspector.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

namespace bh::dlcensus {
namespace {

using namespace bh::gbi;

uint32_t word(const uint8_t* rdram, uint32_t phys) {
    return read_word(rdram, phys);
}

// Element e (row-major) of the fixed-point Mtx at `phys`: 16 integer halves in
// the first eight words, 16 fraction halves in the next eight.
double mtx_element(const uint8_t* rdram, uint32_t phys, int e) {
    const uint32_t iw = word(rdram, phys + 4 * (e / 2));
    const uint32_t fw = word(rdram, phys + 32 + 4 * (e / 2));
    const uint32_t ip = (e % 2 == 0) ? (iw >> 16) & 0xFFFF : iw & 0xFFFF;
    const uint32_t fp = (e % 2 == 0) ? (fw >> 16) & 0xFFFF : fw & 0xFFFF;
    const int32_t fixed = static_cast<int32_t>((ip << 16) | fp);
    return fixed / 65536.0;
}

struct Rect {
    char kind;          // 'F' fill, 'T' texture
    float ulx, uly, lrx, lry;
    uint32_t colour_or_image;
    int scissor;        // index into scissors
};

struct Census {
    const uint8_t* rdram;
    uint32_t segments[16] = {};
    int tris = 0, vtx_loads = 0, calls = 0, commands = 0, matrices = 0, stray = 0;
    uint32_t stray_at[4] = {};
    std::vector<std::pair<uint32_t, uint32_t>> movewords;
    uint32_t fill_colour = 0, image = 0;
    std::vector<std::string> viewports, scissors, projections;
    std::vector<int> tris_under_projection;
    int current_projection = -1;
    int current_scissor = -1;
    std::vector<Rect> rects;
    std::vector<uint32_t> colour_images;

    // ---- the HUD inspector's feed (hud == true) ----------------------------
    //
    // Where each 2D element of the frame lands, in the game's 320x240, for the
    // F1 "Body Harvest HUD" panel. The RSP's own transform state is mirrored
    // closely enough to place a vertex: the modelview stack, the projection, the
    // viewport, and the vertex buffer as it was transformed at load time.
    bool hud = false;
    struct Element {
        std::string identity, second;
        float x0, x1, y0, y1;
        bool rect;
    };
    std::vector<Element> elements;
    using M4 = double[4][4];
    M4 projection = { { 1, 0, 0, 0 }, { 0, 1, 0, 0 }, { 0, 0, 1, 0 }, { 0, 0, 0, 1 } };
    M4 modelview[18] = {};
    int mv_depth = 0;
    float vp_scale_x = 160.0f, vp_scale_y = 120.0f, vp_trans_x = 160.0f, vp_trans_y = 120.0f;
    float to_320 = 1.0f;            // 320 / the colour image's width
    struct Screen { float x, y; bool ok; };
    Screen verts[64] = {};
    uint32_t list_id = 0;           // the called list being walked; 0 = top level
    std::string list_ident;         // its identity (include/bh/hudid.h), taken when it was entered
    std::string texture_ident;      // the current texture's identity, taken at G_SETTIMG (hud only)

    static void read_matrix(const uint8_t* rdram, uint32_t phys, M4& out) {
        for (int e = 0; e < 16; ++e) out[e / 4][e % 4] = mtx_element(rdram, phys, e);
    }
    static void multiply(const M4& a, const M4& b, M4& out) {   // row vectors: out = a * b
        M4 r = {};
        for (int i = 0; i < 4; ++i)
            for (int j = 0; j < 4; ++j)
                for (int k = 0; k < 4; ++k) r[i][j] += a[i][k] * b[k][j];
        std::memcpy(out, r, sizeof r);
    }
    bool projection_is_ortho() const {
        return projection[0][3] == 0.0 && projection[1][3] == 0.0 && projection[2][3] == 0.0 &&
               projection[3][3] != 0.0;
    }
    Screen place(int16_t x, int16_t y, int16_t z) const {
        const double v[4] = { double(x), double(y), double(z), 1.0 };
        double m[4] = {}, c[4] = {};
        const M4& mv = modelview[mv_depth];
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) m[j] += v[k] * mv[k][j];
        for (int j = 0; j < 4; ++j)
            for (int k = 0; k < 4; ++k) c[j] += m[k] * projection[k][j];
        if (c[3] <= 0.0) return { 0, 0, false };
        const double nx = c[0] / c[3], ny = c[1] / c[3];
        return { float((nx * vp_scale_x + vp_trans_x) * to_320), float((-ny * vp_scale_y + vp_trans_y) * to_320), true };
    }
    void note(const std::string& identity, const std::string& second, float x0, float x1, float y0, float y1, bool rect) {
        for (Element& e : elements) {
            if (e.identity == identity && e.rect == rect) {
                e.x0 = std::min(e.x0, x0); e.x1 = std::max(e.x1, x1);
                e.y0 = std::min(e.y0, y0); e.y1 = std::max(e.y1, y1);
                return;
            }
        }
        if (elements.size() < 256) elements.push_back({ identity, second, x0, x1, y0, y1, rect });
    }
    void note_triangle(int a, int b, int c) {
        if (!hud || !projection_is_ortho()) return;
        const Screen* v[3] = { &verts[a & 63], &verts[b & 63], &verts[c & 63] };
        if (!v[0]->ok || !v[1]->ok || !v[2]->ok) return;
        float x0 = v[0]->x, x1 = v[0]->x, y0 = v[0]->y, y1 = v[0]->y;
        for (int i = 1; i < 3; ++i) {
            x0 = std::min(x0, v[i]->x); x1 = std::max(x1, v[i]->x);
            y0 = std::min(y0, v[i]->y); y1 = std::max(y1, v[i]->y);
        }
        note(list_id != 0 ? list_ident : texture_ident,
             list_id != 0 ? texture_ident : std::string(), x0, x1, y0, y1, false);
    }
    void count_tris(int n) {
        tris += n;
        if (current_projection >= 0 && !hud) tris_under_projection[current_projection] += n;
    }

    uint32_t physical(uint32_t address) const {
        const uint32_t seg = (address >> 24) & 0x0F;
        if ((address >> 24) >= 0x80) return address & 0x1FFFFFFF;
        return (segments[seg] + (address & 0x00FFFFFF)) & 0x1FFFFFFF;
    }

    int intern(std::vector<std::string>& list, const std::string& s) {
        for (size_t i = 0; i < list.size(); ++i) {
            if (list[i] == s) return static_cast<int>(i);
        }
        list.push_back(s);
        return static_cast<int>(list.size() - 1);
    }

    void walk(uint32_t address, int depth) {
        if (depth > 12) return;
        uint32_t pc = physical(address);
        for (int guard = 0; guard < 200000; ++guard) {
            if (pc >= 0x800000) return;
            const uint32_t w0 = word(rdram, pc);
            const uint32_t w1 = word(rdram, pc + 4);
            const uint8_t op = static_cast<uint8_t>(w0 >> 24);
            if (!known(op)) {
                if (stray < 4) stray_at[stray] = address;
                ++stray;
                return;
            }
            ++commands;
            pc += 8;
            switch (op) {
                case kEndDl:
                    return;
                case kDl: {
                    ++calls;
                    const bool branch = ((w0 >> 16) & 1) != 0;
                    if (branch) {
                        pc = physical(w1);
                        list_id = w1;
                        if (hud) list_ident = bh::hudid::list(rdram, w1, pc);
                    }
                    else {
                        const uint32_t caller = list_id;
                        const std::string caller_ident = list_ident;
                        list_id = w1;
                        if (hud) list_ident = bh::hudid::list(rdram, w1, physical(w1));
                        walk(w1, depth + 1);
                        list_id = caller;
                        list_ident = caller_ident;
                    }
                    break;
                }
                case kPopMtx:
                    if (w1 == 0) mv_depth = std::max(0, mv_depth - 1);
                    break;
                case kMoveWord:
                    if (movewords.size() < 8) movewords.push_back({ w0, w1 });
                    if ((w0 & 0xFF) == kMwSegment) {
                        segments[(w0 >> 10) & 0x0F] = w1 & 0x1FFFFFFF;
                    }
                    break;
                case kMoveMem:
                    if (((w0 >> 16) & 0xFF) == kMvViewport) {
                        const uint32_t vp = physical(w1);
                        const uint32_t s = word(rdram, vp);
                        const uint32_t t = word(rdram, vp + 8);
                        const int16_t sx = static_cast<int16_t>(s >> 16), sy = static_cast<int16_t>(s);
                        const int16_t tx = static_cast<int16_t>(t >> 16), ty = static_cast<int16_t>(t);
                        vp_scale_x = sx / 4.0f; vp_scale_y = sy / 4.0f;
                        vp_trans_x = tx / 4.0f; vp_trans_y = ty / 4.0f;
                        if (hud) break;
                        char buf[96];
                        std::snprintf(buf, sizeof buf, "scale %.1f,%.1f trans %.1f,%.1f -> x %.1f..%.1f y %.1f..%.1f",
                                      sx / 4.0, sy / 4.0, tx / 4.0, ty / 4.0,
                                      (tx - std::abs(sx)) / 4.0, (tx + std::abs(sx)) / 4.0,
                                      (ty - std::abs(sy)) / 4.0, (ty + std::abs(sy)) / 4.0);
                        intern(viewports, buf);
                    }
                    break;
                case kMtx: {
                    ++matrices;
                    const uint8_t params = static_cast<uint8_t>((w0 >> 16) & 0xFF);
                    if (hud) {
                        M4 m;
                        read_matrix(rdram, physical(w1), m);
                        if ((params & kMtxProjection) != 0) {
                            if (params & kMtxLoad) std::memcpy(projection, m, sizeof m);
                            else multiply(m, projection, projection);
                        }
                        else {
                            if ((params & kMtxPush) != 0 && mv_depth < 17) {
                                std::memcpy(modelview[mv_depth + 1], modelview[mv_depth], sizeof m);
                                ++mv_depth;
                            }
                            if (params & kMtxLoad) std::memcpy(modelview[mv_depth], m, sizeof m);
                            else multiply(m, modelview[mv_depth], modelview[mv_depth]);
                        }
                        break;
                    }
                    if ((params & kMtxProjection) != 0) {
                        const uint32_t m = physical(w1);
                        const double m00 = mtx_element(rdram, m, 0), m11 = mtx_element(rdram, m, 5);
                        const double m22 = mtx_element(rdram, m, 10), m23 = mtx_element(rdram, m, 11);
                        const double m32 = mtx_element(rdram, m, 14), m33 = mtx_element(rdram, m, 15);
                        char buf[160];
                        if (m33 == 0.0 && m23 != 0.0 && m00 != 0.0) {
                            // guPerspective: m22 = (n+f)/(n-f), m32 = 2nf/(n-f) (times the scale)
                            const double scale = -m23;
                            const double a = m22 / scale, b = m32 / scale;
                            const double nearp = b / (a - 1.0), farp = b / (a + 1.0);
                            std::snprintf(buf, sizeof buf, "%s persp aspect %.4f fovy %.2f near %.1f far %.1f",
                                          (params & kMtxLoad) ? "load" : "mul ",
                                          m11 / m00, 2.0 * std::atan(1.0 / (m11 / scale)) * 180.0 / 3.14159265358979,
                                          nearp, farp);
                        }
                        else {
                            std::snprintf(buf, sizeof buf, "%s ortho sx %.5f sy %.5f tx %.3f ty %.3f",
                                          (params & kMtxLoad) ? "load" : "mul ",
                                          m00, m11, mtx_element(rdram, m, 12), mtx_element(rdram, m, 13));
                        }
                        current_projection = intern(projections, buf);
                        if (tris_under_projection.size() < projections.size()) {
                            tris_under_projection.resize(projections.size(), 0);
                        }
                    }
                    break;
                }
                case kVtx: {
                    ++vtx_loads;
                    if (!hud) break;
                    const int count = static_cast<int>((w0 >> 10) & 0x3F);
                    const int first = static_cast<int>((w0 >> 17) & 0x7F);
                    const uint32_t at = physical(w1);
                    for (int i = 0; i < count && first + i < 64; ++i) {
                        const uint32_t xy = word(rdram, at + 16 * i);
                        const uint32_t zf = word(rdram, at + 16 * i + 4);
                        verts[first + i] = place(static_cast<int16_t>(xy >> 16), static_cast<int16_t>(xy),
                                                 static_cast<int16_t>(zf >> 16));
                    }
                    break;
                }
                case kTri1:
                    count_tris(1);
                    note_triangle((w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F);
                    break;
                case kTri2:
                    count_tris(2);
                    note_triangle((w0 >> 17) & 0x7F, (w0 >> 9) & 0x7F, (w0 >> 1) & 0x7F);
                    note_triangle((w1 >> 17) & 0x7F, (w1 >> 9) & 0x7F, (w1 >> 1) & 0x7F);
                    break;
                case kQuad: {
                    count_tris(2);
                    const int a = (w1 >> 25) & 0x7F, b = (w1 >> 17) & 0x7F, c = (w1 >> 9) & 0x7F, d = (w1 >> 1) & 0x7F;
                    note_triangle(a, b, c);
                    note_triangle(a, c, d);
                    break;
                }
                case kSetScissor: {
                    char buf[64];
                    std::snprintf(buf, sizeof buf, "%.1f,%.1f..%.1f,%.1f mode %u",
                                  ((w0 >> 12) & 0xFFF) / 4.0, (w0 & 0xFFF) / 4.0,
                                  ((w1 >> 12) & 0xFFF) / 4.0, (w1 & 0xFFF) / 4.0, (w1 >> 24) & 0xFF);
                    if (!hud) current_scissor = intern(scissors, buf);
                    break;
                }
                case kSetFillColor:
                    fill_colour = w1;
                    break;
                case kSetTImg:
                    image = w1;
                    if (hud) texture_ident = bh::hudid::texture(rdram, w1, physical(w1));
                    break;
                case kSetCImg: {
                    colour_images.push_back(w1);
                    const uint32_t width = (w0 & 0xFFF) + 1;
                    to_320 = width > 0 ? 320.0f / static_cast<float>(width) : 1.0f;
                    break;
                }
                case kFillRect: {
                    const float ulx = ((w1 >> 12) & 0xFFF) / 4.0f, uly = (w1 & 0xFFF) / 4.0f;
                    const float lrx = ((w0 >> 12) & 0xFFF) / 4.0f, lry = (w0 & 0xFFF) / 4.0f;
                    if (hud) {
                        // Full-frame fills are the clears, not the HUD: no identity.
                        const std::string id = bh::hudid::fill(fill_colour, int(ulx * to_320), int(uly * to_320),
                                                               int(lrx * to_320), int(lry * to_320));
                        if (!id.empty()) {
                            note(id, std::string(),
                                 ulx * to_320, (lrx + 1.0f) * to_320, uly * to_320, (lry + 1.0f) * to_320, true);
                        }
                        break;
                    }
                    rects.push_back({ 'F', ulx, uly, lrx, lry, fill_colour, current_scissor });
                    break;
                }
                case kTexRect:
                case kTexRectFlip:
                    if (hud) {
                        note(texture_ident, std::string(),
                             ((w1 >> 12) & 0xFFF) / 4.0f * to_320, ((w0 >> 12) & 0xFFF) / 4.0f * to_320,
                             (w1 & 0xFFF) / 4.0f * to_320, (w0 & 0xFFF) / 4.0f * to_320, true);
                    }
                    else rects.push_back({ 'T', ((w1 >> 12) & 0xFFF) / 4.0f, (w1 & 0xFFF) / 4.0f,
                                      ((w0 >> 12) & 0xFFF) / 4.0f, (w0 & 0xFFF) / 4.0f, image, current_scissor });
                    // RT64's HLE texrect consumes the next two commands (s,t and
                    // dsdx,dtdy) whatever they are.
                    pc += 16;
                    break;
                default:
                    break;
            }
        }
    }
};

uint64_t g_lists = 0;

}  // namespace

void per_frame(uint8_t* rdram, uint32_t list_address) {
    if (!bh::inspector::enabled()) return;

    Census c{ rdram };
    c.hud = true;
    for (int i = 0; i < 4; ++i) c.modelview[0][i][i] = 1.0;
    c.walk(list_address, 0);

    bh::inspector::begin_frame(0);   // no game-state variable identified yet
    for (const auto& e : c.elements) {
        // "As classified" in the panel is the built-in class; the panel shows an
        // override on top of it itself.
        const int given = bh::inspector::builtin_class(e.identity.c_str());
        // BH_HUD_ELEMENTS_LOG=1: every new identity once, with its extent, so tags
        // can be matched to elements from a log.
        static const bool log_elements = std::getenv("BH_HUD_ELEMENTS_LOG") != nullptr;
        if (log_elements) {
            static std::vector<std::string> seen;
            if (seen.size() < 2000 && std::find(seen.begin(), seen.end(), e.identity) == seen.end()) {
                seen.push_back(e.identity);
                std::fprintf(stderr, "[bh-el] %s %s x %.0f..%.0f y %.0f..%.0f %s\n", e.identity.c_str(),
                             e.second.c_str(), e.x0, e.x1, e.y0, e.y1, e.rect ? "rect" : "tris");
            }
        }
        bh::inspector::note_element(e.identity.c_str(), e.second.c_str(), e.x0, e.x1, e.y0, e.y1,
                                    false, given, e.rect);
    }
    bh::inspector::end_frame();
    static bool announced = false;
    if (!announced && !c.elements.empty()) {
        announced = true;
        std::fprintf(stderr, "[bh] HUD inspector: first frame with 2D elements: %zu\n", c.elements.size());
        std::fflush(stderr);
    }
}

bool wanted() {
    static const long every = [] {
        const char* v = std::getenv("BH_DL_CENSUS");
        return v != nullptr ? std::strtol(v, nullptr, 10) : 0L;
    }();
    if (every <= 0) return false;
    return (g_lists++ % static_cast<uint64_t>(every)) == 0;
}

void run(const uint8_t* rdram, uint32_t list_address) {
    Census c{ rdram };
    c.walk(list_address, 0);

    std::fprintf(stderr, "[bh-dl] list %llu at 0x%08X: %d commands, %d calls, %d vtx loads, %d tris, %d matrices, %zu rects, %d walks left the list\n",
                 static_cast<unsigned long long>(g_lists - 1), list_address, c.commands, c.calls,
                 c.vtx_loads, c.tris, c.matrices, c.rects.size(), c.stray);
    for (const auto& mw : c.movewords) {
        std::fprintf(stderr, "[bh-dl]   G_MOVEWORD %08X %08X\n", mw.first, mw.second);
    }
    for (int i = 0; i < c.stray && i < 4; ++i) {
        std::fprintf(stderr, "[bh-dl]   left the list: walk from 0x%08X (segment %u base 0x%08X)\n", c.stray_at[i],
                     (c.stray_at[i] >> 24) & 0x0F, c.segments[(c.stray_at[i] >> 24) & 0x0F]);
    }
    for (uint32_t ci : c.colour_images) {
        std::fprintf(stderr, "[bh-dl]   colour image 0x%08X\n", ci);
    }
    for (size_t i = 0; i < c.viewports.size(); ++i) {
        std::fprintf(stderr, "[bh-dl]   viewport %zu: %s\n", i, c.viewports[i].c_str());
    }
    for (size_t i = 0; i < c.scissors.size(); ++i) {
        std::fprintf(stderr, "[bh-dl]   scissor %zu: %s\n", i, c.scissors[i].c_str());
    }
    for (size_t i = 0; i < c.projections.size(); ++i) {
        std::fprintf(stderr, "[bh-dl]   projection %zu: %s (%d tris)\n", i, c.projections[i].c_str(),
                     i < c.tris_under_projection.size() ? c.tris_under_projection[i] : 0);
    }
    const size_t shown = c.rects.size() < 40 ? c.rects.size() : 40;
    for (size_t i = 0; i < shown; ++i) {
        const Rect& r = c.rects[i];
        std::fprintf(stderr, "[bh-dl]   %s %.1f,%.1f..%.1f,%.1f %s 0x%08X scissor %d\n",
                     r.kind == 'F' ? "fill" : "tex ", r.ulx, r.uly, r.lrx, r.lry,
                     r.kind == 'F' ? "colour" : "image", r.colour_or_image, r.scissor);
    }
    if (c.rects.size() > shown) {
        std::fprintf(stderr, "[bh-dl]   ... %zu more rects\n", c.rects.size() - shown);
    }
    std::fflush(stderr);
}

}  // namespace bh::dlcensus
