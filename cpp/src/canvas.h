#pragma once
// canvas.h - a small HTML-canvas-like 2D API over Dear ImGui's draw list, so the browser game's drawing code ports
// almost line for line. The game draws in its own fixed size (the browser canvas's), scaled to fit the window with
// bars round it. Fill and stroke colours, globalAlpha, lineWidth, shadowBlur (a soft glow), paths, arcs, text with
// textAlign and a baseline like the canvas's, save/restore with translate/rotate/scale.
#include <imgui.h>
#include <string>
#include <vector>

ImU32 rgb(const char* hex, float a = 1.f);              // "#f088e9" -> colour
ImU32 withAlpha(ImU32 c, float a);                       // times a
ImU32 mixCol(ImU32 a, ImU32 b, float t);                 // a..b

class Canvas {
public:
    // the fonts: Consolas (regular and bold) at a few sizes; loaded once before the first frame
    static void loadFonts(const char* regular, const char* bold);

    void begin(ImDrawList* dl, ImVec2 displaySize, float W, float H);   // fit W x H into the window
    void end();
    float W = 960, H = 540;

    // state
    float globalAlpha = 1.f;
    ImU32 fillStyle = IM_COL32_WHITE, strokeStyle = IM_COL32_WHITE;
    float lineWidth = 1.f;
    float shadowBlur = 0.f;                // > 0: a glow round what's drawn, in shadowColor
    ImU32 shadowColor = IM_COL32_WHITE;
    int   textAlign = 0;                   // 0 left, 1 centre, 2 right
    int   textBaseline = 0;                // 0 alphabetic, 1 middle, 2 top
    void  font(float px, bool bold = false) { fontPx_ = px; bold_ = bold; }
    void  setLineDash(float on, float off) { dashOn_ = on; dashOff_ = off; }
    float lineDashOffset = 0.f;

    void save();
    void restore();
    void translate(float x, float y);
    void rotate(float a);
    void scale(float sx, float sy);

    // paths
    void beginPath();
    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void arc(float x, float y, float r, float a0, float a1, bool ccw = false);
    void ellipse(float x, float y, float rx, float ry, float rot, float a0, float a1, bool ccw = false);
    void rect(float x, float y, float w, float h);
    void quadTo(float cx, float cy, float x, float y);
    void closePath();
    void fill();
    void stroke();

    void fillRect(float x, float y, float w, float h);
    void strokeRect(float x, float y, float w, float h);
    void fillText(const std::string& s, float x, float y);
    float measureText(const std::string& s);
    void image(ImTextureID tex, float x, float y, float w, float h, float alpha = 1.f);

    // the curse: tear the picture drawn since begin() into a few shifted bands
    void tear(int bands, float maxShift, float (*rnd)());

    // screen <-> game
    ImVec2 toScreen(float x, float y) const;
    ImVec2 toGame(ImVec2 screen) const { return {(screen.x - o_.x) / s_, (screen.y - o_.y) / s_}; }
    float  pixelScale() const { return s_; }

private:
    struct M { float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0; };   // x' = a x + c y + e, y' = b x + d y + f
    struct Sub { std::vector<ImVec2> pts; bool closed = false; };
    struct Saved { M m; float alpha, lw, blur; ImU32 fill, stroke, shadow; int align, baseline; float px; bool bold;
                   float dashOn, dashOff; };

    ImDrawList* dl_ = nullptr;
    ImVec2 o_{0, 0};
    float s_ = 1.f;
    int vtxStart_ = 0;
    M m_;
    std::vector<Saved> stack_;
    std::vector<Sub> subs_;
    float fontPx_ = 12.f;
    bool bold_ = false;
    float dashOn_ = 0.f, dashOff_ = 0.f;

    ImVec2 P(float x, float y) const;          // game -> screen through the matrix
    float  matScale() const;
    ImU32  col(ImU32 c) const { return withAlpha(c, globalAlpha); }
    Sub&   cur();
    void   fillPoly(const std::vector<ImVec2>& pts, ImU32 c);
    void   strokePoly(const std::vector<ImVec2>& pts, bool closed, ImU32 c, float w);
    void   glowPoly(const std::vector<ImVec2>& pts, bool closed, float baseW);
    ImFont* pickFont(float screenPx) const;
};
