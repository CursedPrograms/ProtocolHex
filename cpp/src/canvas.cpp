// canvas.cpp - the HTML-canvas-like drawing over ImGui's draw list (see canvas.h).
#include "canvas.h"
#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <fstream>

namespace {
constexpr float PI = 3.14159265358979f;
const float SIZES[] = {13, 18, 24, 32, 44, 64, 96, 140};
constexpr int NS = sizeof(SIZES) / sizeof(SIZES[0]);
ImFont* fontsR[NS] = {};
ImFont* fontsB[NS] = {};
}  // namespace

ImU32 rgb(const char* hex, float a) {
    unsigned v = (unsigned)std::strtoul(hex + 1, nullptr, 16);
    if (std::strlen(hex) == 4) {  // #abc
        const unsigned r = (v >> 8) & 15, g = (v >> 4) & 15, b = v & 15;
        v = (r * 17) << 16 | (g * 17) << 8 | (b * 17);
    }
    return IM_COL32((v >> 16) & 255, (v >> 8) & 255, v & 255, (int)(std::clamp(a, 0.f, 1.f) * 255.f + 0.5f));
}

ImU32 withAlpha(ImU32 c, float a) {
    const int al = (int)(((c >> IM_COL32_A_SHIFT) & 0xff) * std::clamp(a, 0.f, 1.f) + 0.5f);
    return (c & ~IM_COL32_A_MASK) | ((ImU32)al << IM_COL32_A_SHIFT);
}

ImU32 mixCol(ImU32 a, ImU32 b, float t) {
    auto ch = [&](int sh) {
        const float x = (float)((a >> sh) & 255), y = (float)((b >> sh) & 255);
        return (ImU32)(x + (y - x) * std::clamp(t, 0.f, 1.f) + 0.5f) << sh;
    };
    return ch(IM_COL32_R_SHIFT) | ch(IM_COL32_G_SHIFT) | ch(IM_COL32_B_SHIFT) | ch(IM_COL32_A_SHIFT);
}

void Canvas::loadFonts(const char* regular, const char* bold) {
    ImGuiIO& io = ImGui::GetIO();
    std::ifstream r(regular), b(bold);
    const bool hasR = r.good(), hasB = b.good();
    ImFontConfig cfg;
    cfg.OversampleH = 2;
    cfg.OversampleV = 1;
    for (int i = 0; i < NS; ++i) {
        if (hasR) fontsR[i] = io.Fonts->AddFontFromFileTTF(regular, SIZES[i], &cfg);
        if (hasB) fontsB[i] = io.Fonts->AddFontFromFileTTF(bold, SIZES[i], &cfg);
        if (!fontsR[i]) fontsR[i] = fontsB[i];
        if (!fontsB[i]) fontsB[i] = fontsR[i];
    }
    if (!fontsR[0]) {
        ImFont* f = io.Fonts->AddFontDefault();
        for (int i = 0; i < NS; ++i) fontsR[i] = fontsB[i] = f;
    }
}

ImFont* Canvas::pickFont(float px) const {
    ImFont* const* set = bold_ ? fontsB : fontsR;
    for (int i = 0; i < NS; ++i)
        if (SIZES[i] >= px * 0.92f) return set[i];
    return set[NS - 1];
}

void Canvas::begin(ImDrawList* dl, ImVec2 disp, float w, float h) {
    W = w; H = h;
    dl_ = dl;
    s_ = std::min(disp.x / W, disp.y / H);
    o_ = {std::floor((disp.x - W * s_) * 0.5f), std::floor((disp.y - H * s_) * 0.5f)};
    dl_->PushClipRect(o_, {o_.x + W * s_, o_.y + H * s_}, true);
    vtxStart_ = dl_->VtxBuffer.Size;
    m_ = M();
    stack_.clear();
    globalAlpha = 1.f; shadowBlur = 0.f; lineWidth = 1.f; textAlign = 0; textBaseline = 0;
    dashOn_ = dashOff_ = 0.f;
}

void Canvas::end() { dl_->PopClipRect(); }

ImVec2 Canvas::toScreen(float x, float y) const { return {o_.x + x * s_, o_.y + y * s_}; }

ImVec2 Canvas::P(float x, float y) const {
    return toScreen(m_.a * x + m_.c * y + m_.e, m_.b * x + m_.d * y + m_.f);
}
float Canvas::matScale() const { return std::sqrt(std::fabs(m_.a * m_.d - m_.b * m_.c)); }

void Canvas::save() {
    stack_.push_back({m_, globalAlpha, lineWidth, shadowBlur, fillStyle, strokeStyle, shadowColor, textAlign,
                      textBaseline, fontPx_, bold_, dashOn_, dashOff_});
}
void Canvas::restore() {
    if (stack_.empty()) return;
    const Saved& s = stack_.back();
    m_ = s.m; globalAlpha = s.alpha; lineWidth = s.lw; shadowBlur = s.blur; fillStyle = s.fill; strokeStyle = s.stroke;
    shadowColor = s.shadow; textAlign = s.align; textBaseline = s.baseline; fontPx_ = s.px; bold_ = s.bold;
    dashOn_ = s.dashOn; dashOff_ = s.dashOff;
    stack_.pop_back();
}
void Canvas::translate(float x, float y) { m_.e += m_.a * x + m_.c * y; m_.f += m_.b * x + m_.d * y; }
void Canvas::rotate(float t) {
    const float c = std::cos(t), s = std::sin(t);
    const M o = m_;
    m_.a = o.a * c + o.c * s; m_.b = o.b * c + o.d * s;
    m_.c = -o.a * s + o.c * c; m_.d = -o.b * s + o.d * c;
}
void Canvas::scale(float sx, float sy) { m_.a *= sx; m_.b *= sx; m_.c *= sy; m_.d *= sy; }

// ---- paths ----------------------------------------------------------------------------------------------------------
void Canvas::beginPath() { subs_.clear(); }
Canvas::Sub& Canvas::cur() { if (subs_.empty() || subs_.back().closed) subs_.push_back({}); return subs_.back(); }
void Canvas::moveTo(float x, float y) { subs_.push_back({}); subs_.back().pts.push_back(P(x, y)); }
void Canvas::lineTo(float x, float y) { cur().pts.push_back(P(x, y)); }
void Canvas::closePath() {
    if (subs_.empty()) return;
    subs_.back().closed = true;
}

void Canvas::ellipse(float x, float y, float rx, float ry, float rot, float a0, float a1, bool ccw) {
    float sweep;
    if (!ccw) { sweep = a1 - a0; if (sweep >= 2 * PI) sweep = 2 * PI; else { sweep = std::fmod(sweep, 2 * PI); if (sweep < 0) sweep += 2 * PI; } }
    else { sweep = a1 - a0; if (sweep <= -2 * PI) sweep = -2 * PI; else { sweep = std::fmod(sweep, 2 * PI); if (sweep > 0) sweep -= 2 * PI; } }
    const float rs = std::max(rx, ry) * s_ * matScale();
    int n = (int)std::ceil(std::fabs(sweep) * std::max(2.f, rs) / 4.f);
    n = std::clamp(n, 3, 96);
    Sub& sb = cur();
    const float cr = std::cos(rot), sr = std::sin(rot);
    for (int i = 0; i <= n; ++i) {
        const float a = a0 + sweep * i / n;
        const float ex = std::cos(a) * rx, ey = std::sin(a) * ry;
        sb.pts.push_back(P(x + ex * cr - ey * sr, y + ex * sr + ey * cr));
    }
}
void Canvas::arc(float x, float y, float r, float a0, float a1, bool ccw) { ellipse(x, y, r, r, 0.f, a0, a1, ccw); }
void Canvas::rect(float x, float y, float w, float h) {
    moveTo(x, y); lineTo(x + w, y); lineTo(x + w, y + h); lineTo(x, y + h); closePath();
}
void Canvas::quadTo(float cx, float cy, float x, float y) {
    Sub& sb = cur();
    if (sb.pts.empty()) { sb.pts.push_back(P(cx, cy)); }
    // the start in game space: invert the last screen point is awkward, so sample in screen space
    const ImVec2 p0 = sb.pts.back(), p1 = P(cx, cy), p2 = P(x, y);
    for (int i = 1; i <= 12; ++i) {
        const float t = i / 12.f, u = 1 - t;
        sb.pts.push_back({u * u * p0.x + 2 * u * t * p1.x + t * t * p2.x, u * u * p0.y + 2 * u * t * p1.y + t * t * p2.y});
    }
}

static bool convex(const std::vector<ImVec2>& p) {
    const size_t n = p.size();
    if (n < 4) return true;
    int sign = 0;
    for (size_t i = 0; i < n; ++i) {
        const ImVec2 a = p[i], b = p[(i + 1) % n], c = p[(i + 2) % n];
        const float z = (b.x - a.x) * (c.y - b.y) - (b.y - a.y) * (c.x - b.x);
        if (std::fabs(z) < 1e-4f) continue;
        const int s = z > 0 ? 1 : -1;
        if (sign && s != sign) return false;
        sign = s;
    }
    return true;
}

void Canvas::fillPoly(const std::vector<ImVec2>& pts, ImU32 c) {
    if (pts.size() < 3) return;
    if (convex(pts)) { dl_->AddConvexPolyFilled(pts.data(), (int)pts.size(), c); return; }
    // not convex: a fan from the middle (fine for the star-ish shapes the games draw)
    ImVec2 m{0, 0};
    for (const ImVec2& p : pts) { m.x += p.x; m.y += p.y; }
    m.x /= pts.size(); m.y /= pts.size();
    for (size_t i = 0; i < pts.size(); ++i) dl_->AddTriangleFilled(m, pts[i], pts[(i + 1) % pts.size()], c);
}

void Canvas::strokePoly(const std::vector<ImVec2>& pts, bool closed, ImU32 c, float w) {
    if (pts.size() < 2) return;
    if (dashOn_ <= 0.f) { dl_->AddPolyline(pts.data(), (int)pts.size(), c, closed ? ImDrawFlags_Closed : 0, w); return; }
    const double on = std::max(0.5, (double)dashOn_ * s_), off = std::max(0.5, (double)dashOff_ * s_), period = on + off;
    double phase = std::fmod((double)lineDashOffset * s_, period);   // where in the on+off pattern the line starts
    if (phase < 0) phase += period;
    const size_t n = closed ? pts.size() : pts.size() - 1;
    for (size_t i = 0; i < n; ++i) {
        const ImVec2 a = pts[i], b = pts[(i + 1) % pts.size()];
        const double len = std::hypot((double)b.x - a.x, (double)b.y - a.y);
        double d = 0.0;
        int guard = 0;
        while (d < len - 1e-6 && guard++ < 10000) {
            const bool drawOn = phase < on;
            const double step = std::min(drawOn ? on - phase : period - phase, len - d);
            if (drawOn) {
                const float t0 = (float)(d / len), t1 = (float)((d + step) / len);
                dl_->AddLine({a.x + (b.x - a.x) * t0, a.y + (b.y - a.y) * t0}, {a.x + (b.x - a.x) * t1, a.y + (b.y - a.y) * t1}, c, w);
            }
            d += step;
            phase += step;
            if (phase >= period - 1e-9) phase = 0.0;
            else if (drawOn && phase >= on - 1e-9) phase = on;
        }
    }
}

void Canvas::glowPoly(const std::vector<ImVec2>& pts, bool closed, float baseW) {
    if (shadowBlur <= 0.f || pts.size() < 2) return;
    const float b = shadowBlur * s_;
    const float dOn = dashOn_;
    dashOn_ = 0.f;
    for (int i = 3; i >= 1; --i)
        dl_->AddPolyline(pts.data(), (int)pts.size(), withAlpha(shadowColor, 0.11f * globalAlpha),
                         closed ? ImDrawFlags_Closed : 0, baseW + b * i * 0.42f);
    dashOn_ = dOn;
}

void Canvas::fill() {
    for (Sub& sb : subs_) {
        glowPoly(sb.pts, true, 0.f);
        fillPoly(sb.pts, col(fillStyle));
    }
}
void Canvas::stroke() {
    const float w = std::max(0.6f, lineWidth * s_ * matScale());
    for (Sub& sb : subs_) {
        glowPoly(sb.pts, sb.closed, w);
        strokePoly(sb.pts, sb.closed, col(strokeStyle), w);
    }
}

void Canvas::fillRect(float x, float y, float w, float h) {
    const bool axis = m_.b == 0 && m_.c == 0;
    if (axis && shadowBlur <= 0.f) {
        ImVec2 a = P(x, y), b = P(x + w, y + h);
        if (a.x > b.x) std::swap(a.x, b.x);
        if (a.y > b.y) std::swap(a.y, b.y);
        dl_->AddRectFilled(a, b, col(fillStyle));
        return;
    }
    if (axis) {  // a glow: widening, fading rounded rectangles round it
        ImVec2 a = P(x, y), b = P(x + w, y + h);
        if (a.x > b.x) std::swap(a.x, b.x);
        if (a.y > b.y) std::swap(a.y, b.y);
        const float bl = shadowBlur * s_;
        for (int i = 3; i >= 1; --i) {
            const float e = bl * i / 3.f * 0.6f;
            dl_->AddRectFilled({a.x - e, a.y - e}, {b.x + e, b.y + e}, withAlpha(shadowColor, 0.10f * globalAlpha), e);
        }
        dl_->AddRectFilled(a, b, col(fillStyle));
        return;
    }
    std::vector<Sub> keep;
    keep.swap(subs_);
    beginPath(); rect(x, y, w, h); fill();
    subs_.swap(keep);
}

void Canvas::strokeRect(float x, float y, float w, float h) {
    std::vector<Sub> keep;
    keep.swap(subs_);
    beginPath(); rect(x, y, w, h); stroke();
    subs_.swap(keep);
}

void Canvas::fillText(const std::string& str, float x, float y) {
    if (str.empty()) return;
    const float px = fontPx_ * s_ * matScale();
    ImFont* f = pickFont(px);
    const float k = px / f->FontSize;
    const float w = f->CalcTextSizeA(px, FLT_MAX, 0.f, str.c_str()).x;
    ImVec2 p = P(x, y);
    if (textAlign == 1) p.x -= w * 0.5f;
    else if (textAlign == 2) p.x -= w;
    if (textBaseline == 0) p.y -= f->Ascent * k;
    else if (textBaseline == 1) p.y -= (f->Ascent - f->Descent) * k * 0.5f;
    dl_->AddText(f, px, {std::floor(p.x + 0.5f), std::floor(p.y + 0.5f)}, col(fillStyle), str.c_str());
}

float Canvas::measureText(const std::string& str) {
    const float px = fontPx_ * s_;
    ImFont* f = pickFont(px);
    return f->CalcTextSizeA(px, FLT_MAX, 0.f, str.c_str()).x / s_;
}

void Canvas::image(ImTextureID tex, float x, float y, float w, float h, float alpha) {
    if (!tex) return;
    dl_->AddImage(tex, P(x, y), P(x + w, y + h), {0, 0}, {1, 1}, IM_COL32(255, 255, 255, (int)(255 * alpha * globalAlpha)));
}

void Canvas::tear(int bands, float maxShift, float (*rnd)()) {
    for (int i = 0; i < bands; ++i) {
        const float gy = rnd() * H, gh = 6 + rnd() * 30, gx = (rnd() - 0.5f) * maxShift;
        const float y0 = o_.y + gy * s_, y1 = o_.y + (gy + gh) * s_, dx = gx * s_;
        for (int v = vtxStart_; v < dl_->VtxBuffer.Size; ++v) {
            ImDrawVert& vx = dl_->VtxBuffer[v];
            if (vx.pos.y >= y0 && vx.pos.y < y1) vx.pos.x += dx;
        }
    }
}
