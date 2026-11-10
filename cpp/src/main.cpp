// main.cpp - PROTOCOL.hex: the window, the splash (the title, the Cursed logo, the copyright), the game loop.
//
//   any key / click   start (on the splash)       Arrows / WASD   move
//   Space   rot the wall ahead                     Shift   speed burst
//   C       plant a trap                           Q       cloak
//   F       split off a copy                       P       pause
//   M       mute                                   F11     fullscreen
//   Esc     back to the splash (from the splash: quit)
#include "canvas.h"
#include "game.h"
#include "synth.h"

#include <SDL.h>
#include <SDL_opengl.h>
#include <imgui.h>
#include <imgui_impl_opengl3.h>
#include <imgui_impl_sdl2.h>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>
#include <vector>

namespace {

std::string base;                                   // the folder the exe is in
std::string asset(const char* name) { return base + "assets/" + name; }

struct Texture { GLuint id = 0; int w = 0, h = 0; };

Texture loadTexture(const std::string& path) {
    Texture t;
    int n = 0;
    unsigned char* px = stbi_load(path.c_str(), &t.w, &t.h, &n, 4);
    if (!px) return t;
    glGenTextures(1, &t.id);
    glBindTexture(GL_TEXTURE_2D, t.id);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, t.w, t.h, 0, GL_RGBA, GL_UNSIGNED_BYTE, px);
    stbi_image_free(px);
    return t;
}

// text with letter spacing, centred on cx
void spaced(Canvas& c, const std::string& s, float cx, float y, float spacing) {
    float w = 0;
    for (char ch : s) w += c.measureText(std::string(1, ch)) + spacing;
    w -= spacing;
    float x = cx - w * 0.5f;
    const int align = c.textAlign;
    c.textAlign = 0;
    for (char ch : s) { const std::string g(1, ch); c.fillText(g, x, y); x += c.measureText(g) + spacing; }
    c.textAlign = align;
}

void splash(Canvas& c, const Texture& logo, double t) {
    const ImU32 green = rgb("#39ff6a"), pink = rgb("#f088e9"), sky = rgb("#34a9e3"), grey = rgb("#8fa3b3");
    c.fillStyle = rgb("#020611"); c.fillRect(0, 0, c.W, c.H);
    const float cx = c.W / 2;
    // the title: PROTOCOL in terminal green, .hex in pink
    c.font(80, true);
    const float sp = 80 * 0.06f;
    float wP = 0, wH = 0;
    for (char ch : std::string("PROTOCOL")) wP += c.measureText(std::string(1, ch)) + sp;
    for (char ch : std::string(".hex")) wH += c.measureText(std::string(1, ch)) + sp;
    float x = cx - (wP + wH - sp) * 0.5f;
    c.textAlign = 0;
    for (char ch : std::string("PROTOCOL.hex")) {
        const std::string g(1, ch);
        c.fillStyle = x < cx - (wP + wH - sp) * 0.5f + wP - 1 ? green : pink;
        c.fillText(g, x, 172);
        x += c.measureText(g) + sp;
    }
    c.font(15); c.fillStyle = sky;
    spaced(c, "AN ARCADE PHANTASM GAME", cx, 216, 15 * 0.25f);
    c.font(12.5f); c.fillStyle = green; c.textAlign = 1;
    const std::string blurb = "> you are the virus. infect the system. rot its walls. stay out of sight.";
    c.fillText(blurb, cx, 246);
    if (std::fmod(t, 1.2) < 0.6) { c.textAlign = 0; c.fillText("_", cx + c.measureText(blurb) * 0.5f, 246); c.textAlign = 1; }
    if (logo.id) {
        const float lw = 200, lh = lw * logo.h / logo.w;
        c.image((ImTextureID)(intptr_t)logo.id, cx - lw / 2, 266, lw, lh);
    }
    c.font(12); c.fillStyle = grey;
    c.fillText("\xC2\xA9 Cursed Entertainment 2026", cx, 392);
    if (std::fmod(t, 1.2) < 0.6) { c.font(16); c.fillStyle = pink; spaced(c, "Press any key or click to start", cx, 432, 16 * 0.08f); }
    c.font(11); c.fillStyle = rgb("#5d7385"); c.textAlign = 1;
    c.fillText("Arrows / WASD move  \xC2\xB7  Space rot  \xC2\xB7  Shift burst  \xC2\xB7  C trap  \xC2\xB7  Q cloak  \xC2\xB7  F split  "
               "\xC2\xB7  P pause  \xC2\xB7  M mute  \xC2\xB7  F11 fullscreen  \xC2\xB7  Esc quit", cx, c.H - 22);
}

}  // namespace

int main(int argc, char** argv) {
    char* bp = SDL_GetBasePath();
    base = bp ? bp : "";
    SDL_free(bp);

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO | SDL_INIT_TIMER) != 0) return 1;
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 3);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_MULTISAMPLESAMPLES, 4);
    SDL_Window* win = SDL_CreateWindow("PROTOCOL.hex", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, 1280, 720,
                                       SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_ALLOW_HIGHDPI);
    if (!win) return 1;
    SDL_GLContext gl = SDL_GL_CreateContext(win);
    SDL_GL_MakeCurrent(win, gl);
    SDL_GL_SetSwapInterval(1);

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    Canvas::loadFonts("C:/Windows/Fonts/consola.ttf", "C:/Windows/Fonts/consolab.ttf");
    ImGui_ImplSDL2_InitForOpenGL(win, gl);
    ImGui_ImplOpenGL3_Init("#version 330");

    const Texture cursed = loadTexture(asset("cursed-logo.png"));
    if (SDL_Surface* icon = SDL_LoadBMP(asset("icon.bmp").c_str())) { SDL_SetWindowIcon(win, icon); SDL_FreeSurface(icon); }
    Synth::open(0.3f);

    // the best score and the mute choice, kept in the user's settings folder
    char* pp = SDL_GetPrefPath("Cursed Entertainment", "PROTOCOL.hex");
    Game::init(pp ? pp : "");
    SDL_free(pp);

    bool started = false, fullscreen = false, quit = false;
    std::string capture;                            // --capture file.bmp seconds: save one frame of the window, then quit
    double captureAt = 0;
    for (int i = 1; i < argc; ++i) {
        const std::string a = argv[i];
        if (a == "--shot") { started = true; Game::shot(); }
        else if (a == "--capture" && i + 2 < argc) { capture = argv[i + 1]; captureAt = std::atof(argv[i + 2]); i += 2; }
        else if (a == "--sector" && i + 1 < argc) { started = true; Game::load(std::atoi(argv[++i]) - 1); }
    }
    Canvas canvas;
    Uint64 last = SDL_GetPerformanceCounter();
    double acc = 0;

    while (!quit) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            ImGui_ImplSDL2_ProcessEvent(&e);
            if (e.type == SDL_QUIT) quit = true;
            if (e.type == SDL_MOUSEBUTTONDOWN && e.button.button == SDL_BUTTON_LEFT) {
                if (!started) { started = true; Game::newGame(); }
                else Game::next();
            }
            if (e.type != SDL_KEYDOWN || e.key.repeat) continue;
            const SDL_Keycode k = e.key.keysym.sym;
            if (k == SDLK_F11 || (k == SDLK_RETURN && (e.key.keysym.mod & KMOD_ALT))) {
                fullscreen = !fullscreen;
                SDL_SetWindowFullscreen(win, fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                continue;
            }
            if (k == SDLK_ESCAPE) { if (!started) quit = true; else started = false; continue; }
            if (!started) { started = true; Game::newGame(); continue; }
            if (Game::next()) continue;
            switch (k) {
            case SDLK_UP: case SDLK_w: Game::key(Game::Up); break;
            case SDLK_DOWN: case SDLK_s: Game::key(Game::Down); break;
            case SDLK_LEFT: case SDLK_a: Game::key(Game::Left); break;
            case SDLK_RIGHT: case SDLK_d: Game::key(Game::Right); break;
            case SDLK_SPACE: Game::key(Game::Rot); break;
            case SDLK_LSHIFT: case SDLK_RSHIFT: Game::key(Game::Burst); break;
            case SDLK_c: Game::key(Game::Trap); break;
            case SDLK_q: Game::key(Game::Cloak); break;
            case SDLK_f: Game::key(Game::Split); break;
            case SDLK_p: Game::key(Game::Pause); break;
            case SDLK_m: Game::key(Game::Mute); break;
            default: break;
            }
        }

        const Uint64 now = SDL_GetPerformanceCounter();
        acc += std::min(0.1, (double)(now - last) / (double)SDL_GetPerformanceFrequency());
        last = now;
        constexpr double STEP = 1.0 / 60.0;
        while (acc >= STEP) { acc -= STEP; if (started) Game::update(STEP); }
        const double t = SDL_GetTicks() / 1000.0;

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL2_NewFrame();
        ImGui::NewFrame();
        canvas.begin(ImGui::GetBackgroundDrawList(), io.DisplaySize, 960, 540);
        if (!started) splash(canvas, cursed, t);
        else Game::draw(canvas);
        canvas.end();

        ImGui::Render();
        int w, h;
        SDL_GL_GetDrawableSize(win, &w, &h);
        glViewport(0, 0, w, h);
        glClearColor(0.008f, 0.024f, 0.067f, 1.f);
        glClear(GL_COLOR_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
        if (!capture.empty() && t >= captureAt) {
            std::vector<unsigned char> px((size_t)w * h * 4), flip((size_t)w * h * 4);
            glPixelStorei(GL_PACK_ALIGNMENT, 1);
            glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px.data());
            for (int y = 0; y < h; ++y) std::copy_n(&px[(size_t)(h - 1 - y) * w * 4], (size_t)w * 4, &flip[(size_t)y * w * 4]);
            for (size_t i = 3; i < flip.size(); i += 4) flip[i] = 255;
            if (SDL_Surface* s = SDL_CreateRGBSurfaceWithFormatFrom(flip.data(), w, h, 32, w * 4, SDL_PIXELFORMAT_ABGR8888)) {
                SDL_SaveBMP(s, capture.c_str());
                SDL_FreeSurface(s);
            }
            quit = true;
        }
        SDL_GL_SwapWindow(win);
    }

    Synth::close();
    ImGui_ImplOpenGL3_Shutdown();
    ImGui_ImplSDL2_Shutdown();
    ImGui::DestroyContext();
    SDL_GL_DeleteContext(gl);
    SDL_DestroyWindow(win);
    SDL_Quit();
    return 0;
}
