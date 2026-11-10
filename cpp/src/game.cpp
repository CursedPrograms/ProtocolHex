// game.cpp - PROTOCOL.hex, ported from the browser game (site/game.js) rule for rule.
// You are a virus loose inside a computer. Slide along the glowing pathways of the board, infect every node (they
// turn pink and become yours) and keep away from the green antivirus. Infection energy lets you corrupt the system
// itself: rot a wall into a new pathway, break into sealed vaults, cut a shortcut or escape. Each sector brings a new
// security system: a firewall, quarantine zones, a scanner, honeypots and, last, System Restore.
// The sectors are built from the same seeds as the browser's, so they're the same boards.
#include "game.h"
#include "synth.h"

#include <algorithm>
#include <cmath>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <functional>
#include <random>
#include <string>
#include <vector>

namespace {

constexpr float W = 960, H = 540, CS = 30, OX = 15, OY = 50;
constexpr int COLS = 31, ROWS = 15;
constexpr double STEP = 1.0 / 60.0, PI = 3.14159265358979;
constexpr int WALL = 1, PATH = 0, ROT = 2;
constexpr int ROT_COST = 2, TRAP_COST = 3, MAX_ENERGY = 12;

namespace C {
const ImU32 navy = rgb("#001725"), deep = rgb("#005184"), blue = rgb("#007ab7"), sky = rgb("#34a9e3"),
            pink = rgb("#f088e9"), green = rgb("#39ff6a"), black = rgb("#020611"), white = rgb("#ffffff");
}

struct Dir { int dx, dy; };
const Dir UP{0, -1}, DOWN{0, 1}, LEFT{-1, 0}, RIGHT{1, 0};
const Dir DLIST[4] = {UP, RIGHT, DOWN, LEFT};
bool same(Dir a, Dir b) { return a.dx == b.dx && a.dy == b.dy; }

struct Sector {
    const char* name; const char* sys; int av, nodes, vaults, honey, restore; const char* unlock;
    const char* intro; const char* tip;
};
const Sector SECTORS[] = {
    {"BOOT SECTOR", "antivirus", 2, 16, 2, 0, 0, "rot",
     "Infect every node. Green antivirus hunt you down the traces.", "SPACE rots the wall ahead into a new path (2 energy). Vaults hide fat nodes."},
    {"FIREWALL", "firewall", 2, 18, 2, 0, 0, "burst",
     "A firewall sweeps the board. It can't see into your rot: hide in a rotted cell.", "SHIFT: speed burst."},
    {"QUARANTINE", "quarantine", 3, 18, 2, 0, 0, "trap",
     "Quarantine zones seal shut. Get locked in and a guard spawns beside you.", "C: drop a corruption trap that snares antivirus (3 energy)."},
    {"SCANNER", "scanner", 3, 18, 3, 0, 0, "cloak",
     "The scanner's beam reveals you to every antivirus at once.", "Q: cloak as harmless data. Nothing sees you."},
    {"HONEYPOT", "honeypot", 3, 18, 2, 4, 0, "split",
     "Some nodes are honeypots. Watch for the flicker of a '?'.", "F: split off a copy. It infects what it touches, and draws fire."},
    {"SYSTEM RESTORE", "restore", 4, 18, 0, 0, 4, nullptr,
     "System Restore undoes your corruption, wave after wave.", "Rot into the four restore points and infect them to break it."},
};
constexpr int NSECTORS = 6;
struct Abil { const char* id; const char* key; const char* name; };
const Abil ABIL[] = {{"rot", "SPACE", "ROT"}, {"burst", "SHIFT", "BURST"}, {"trap", "C", "TRAP"},
                     {"cloak", "Q", "CLOAK"}, {"split", "F", "SPLIT"}};

// ---- the browser's random numbers ------------------------------------------------------------------------------------
std::mt19937 mt{std::random_device{}()};
double mrand() { return std::uniform_real_distribution<double>(0.0, 1.0)(mt); }   // Math.random()
float randomf() { return (float)mrand(); }

struct Rng {                                         // the browser's seeded rng (mulberry32), bit for bit
    uint32_t seed;
    explicit Rng(uint32_t s) : seed(s) {}
    double operator()() {
        seed = seed + 0x6D2B79F5u;
        uint32_t t = (seed ^ (seed >> 15)) * (1u | seed);
        t = (t + ((t ^ (t >> 7)) * (61u | t))) ^ t;
        return (double)(t ^ (t >> 14)) / 4294967296.0;
    }
};

// ---- the state -------------------------------------------------------------------------------------------------------
struct Ent {
    int c = 0, r = 0;
    bool hasDir = false, hasWant = false;
    Dir dir{0, 0}, want{0, 0}, face{-1, 0};
    double prog = 0, stun = 0, inv = 0, life = 0, blink = 0;
    bool guard = false;
    int id = 0;
};
struct Node { int c, r; bool inf, vault, restore, honey, dead; double flick; };
struct Cell { int c, r; double j; };
struct Zone { int c0, r0, c1, r1; double t; int off; int state; int guard; };   // state 0 open, 1 warn, 2 locked
struct Particle { float x, y, vx, vy, life, max; ImU32 c; };
struct LogLine { std::string s; double t; };
struct Rotting { int c, r; double t; };
struct Trap { int c, r; };

std::string saveDir;
int best = 0, reached = 0;
bool god = false, practice = false;           // practice: --sector / --shot, no best score

int sector = 0, cycle = 0, score = 0, lives = 3;
double energy = 4;
enum State { Play, Dying, Clear, Over, Won } state = Play;
double stateT = 0;
bool isPaused = false;
bool frozen = false;                                 // --shot: a still moment, nothing moves

int grid[ROWS][COLS];
bool hard[ROWS][COLS];
std::vector<Node> nodes;
std::vector<Ent> avs, decoys;
std::vector<Trap> traps;
std::vector<Rotting> rotting;
std::vector<Zone> zones;
struct Firewall { bool on = false; int phase = 0; double t = 0, x = 0; int dir = 1; } firewall;   // phase 0 wait 1 warn 2 sweep
struct Scanner { bool on = false; double a = 0, cool = 0; } scanner;
struct Restore { bool on = false; double t = 0; int phase = 0; double R = 0, prevR = 0; int cc = 15, cr = 7; } restore;   // 0 wait 1 warn 2 wave 3 broken
Ent player;
Cell startCell{1, 7, 0};
std::vector<Cell> avSpawns;
std::vector<Particle> particles;
std::vector<LogLine> logLines;
double glitchT = 0, introT = 0, alertT = 0, timeS = 0;
std::vector<std::vector<int>> distField;
bool hasField = false;
struct { double t = 0, cd = 0; } aBurst, aCloak, aSplit;
int nextId = 1;

// ---- the sound effects (site/sfx.js) -----------------------------------------------------------------------------------
namespace Sfx {
using namespace Synth;
double lastInfect = -10; int chain = 0;
void infect() {
    const double t = now();
    chain = t - lastInfect < 1.2 ? std::min(chain + 1, 8) : 0; lastInfect = t;
    const float f = 440.f * std::pow(1.122f, (float)chain);
    tone(Square, f, f * 1.5f, t, 0.08f, 0.12f); tone(Square, f * 2, f * 2, t + 0.06, 0.08f, 0.07f);
}
void rot() { const double t = now(); noise(t, 0.35f, 0.35f, 600, Bandpass); tone(Saw, 180, 60, t, 0.35f, 0.1f); }
void denied() { const double t = now(); tone(Square, 140, 140, t, 0.1f, 0.12f); tone(Square, 140, 140, t + 0.14, 0.1f, 0.12f); }
void trap() { tone(Triangle, 600, 200, now(), 0.2f, 0.2f); }
void snare() { const double t = now(); noise(t, 0.2f, 0.3f, 3000, Bandpass); tone(Square, 900, 300, t, 0.25f, 0.1f); }
void burst() { tone(Saw, 200, 1200, now(), 0.25f, 0.08f); }
void cloak() { const double t = now(); tone(Sine, 900, 300, t, 0.4f, 0.2f); tone(Sine, 1350, 450, t, 0.4f, 0.1f); }
void split() { const double t = now(); tone(Square, 500, 500, t, 0.06f, 0.1f); tone(Square, 750, 750, t + 0.07, 0.06f, 0.1f); tone(Square, 1000, 1000, t + 0.14, 0.06f, 0.1f); }
void alarm() { const double t = now(); for (int i = 0; i < 3; i++) tone(Square, 880, 660, t + i * 0.18, 0.16f, 0.09f); }
void honey() { const double t = now(); tone(Saw, 300, 80, t, 0.6f, 0.15f); noise(t, 0.4f, 0.3f, 800, Bandpass); }
void sweep() { noise(now(), 0.8f, 0.15f, 400, Bandpass); }
void restoreS() { const double t = now(); tone(Sine, 220, 880, t, 1.2f, 0.18f); tone(Sine, 330, 1320, t, 1.2f, 0.08f); }
void die() { const double t = now(); noise(t, 0.6f, 0.45f, 1200, Bandpass); tone(Square, 600, 40, t, 0.7f, 0.18f); }
void clear() {
    const double t = now();
    const float fs[] = {392.f, 523.3f, 659.3f, 784.f, 1046.5f};
    for (int i = 0; i < 5; i++) { tone(Square, fs[i], fs[i], t + i * 0.08, 0.15f, 0.08f); tone(Triangle, fs[i], fs[i], t + i * 0.08, 0.4f, 0.15f); }
}
}  // namespace Sfx

// ---- saving ---------------------------------------------------------------------------------------------------------
int loadInt(const char* name) { std::ifstream f(saveDir + name); int v = 0; f >> v; return v; }
void saveInt(const char* name, int v) { std::ofstream f(saveDir + name); f << v; }

// ---- helpers ----------------------------------------------------------------------------------------------------------
bool has(const std::string& id) {
    const char* ids[] = {"rot", "burst", "trap", "cloak", "split"};
    int i = 0;
    while (i < 5 && id != ids[i]) ++i;
    return cycle > 0 || i <= sector;
}
bool inside(int c, int r) { return c >= 0 && r >= 0 && c < COLS && r < ROWS; }
bool open(int c, int r) { return inside(c, r) && grid[r][c] != WALL; }
void log(std::string s) {
    if (s.size() > 68) s = s.substr(0, 66) + "..";
    logLines.push_back({s, timeS});
    if (logLines.size() > 4) logLines.erase(logLines.begin());
}
std::string hex(int n) {
    char b[16];
    std::snprintf(b, sizeof b, "0x%04X", n & 0xffff);
    return b;
}
std::string lower(std::string s) { for (char& ch : s) ch = (char)(ch == ' ' ? '_' : std::tolower((unsigned char)ch)); return s; }

using Field = std::vector<std::vector<int>>;
template <class Pass>
Field bfs(const std::vector<Cell>& from, Pass pass) {
    Field d(ROWS, std::vector<int>(COLS, -1));
    std::vector<Cell> q;
    for (const Cell& p : from) if (inside(p.c, p.r)) { d[p.r][p.c] = 0; q.push_back(p); }
    for (size_t i = 0; i < q.size(); i++) {
        const Cell p = q[i];
        for (int k = 0; k < 4; k++) {
            const int nc = p.c + DLIST[k].dx, nr = p.r + DLIST[k].dy;
            if (inside(nc, nr) && d[nr][nc] < 0 && pass(p.c, p.r, nc, nr)) { d[nr][nc] = d[p.r][p.c] + 1; q.push_back({nc, nr, 0}); }
        }
    }
    return d;
}

float px(double c) { return (float)(OX + (c + 0.5) * CS); }
float py(double r) { return (float)(OY + (r + 0.5) * CS); }

void burst(float x, float y, int n, float speed, std::vector<ImU32> cols) {
    if (particles.size() > 800) return;
    for (int i = 0; i < n; i++) {
        const float a = (float)(mrand() * PI * 2), v = speed * (float)(0.2 + mrand() * 0.8), life = (float)(25 + mrand() * 35);
        particles.push_back({x, y, std::cos(a) * v, std::sin(a) * v, life, life, cols[(size_t)(mrand() * cols.size())]});
    }
}

// ---- building a sector ---------------------------------------------------------------------------------------------------
Ent makeAV(int c, int r, bool guard) {
    Ent e; e.c = c; e.r = r; e.guard = guard; e.face = LEFT; e.blink = mrand() * 6; e.id = nextId++;
    return e;
}
void spawnPlayer() {
    player = Ent(); player.c = startCell.c; player.r = startCell.r; player.face = RIGHT; player.inv = 2; player.id = 0;
}
bool nodeAt(int c, int r) { for (const Node& o : nodes) if (o.c == c && o.r == r) return true; return false; }

void build() {
    const Sector& S = SECTORS[sector];
    Rng rnd((uint32_t)(1009 + sector * 7919 + cycle * 104729));
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) { grid[r][c] = WALL; hard[r][c] = r == 0 || c == 0 || r == ROWS - 1 || c == COLS - 1; }
    // a maze through the odd cells, then braided so there are loops to run round
    std::vector<Cell> stack{{1, 1, 0}}; grid[1][1] = PATH;
    while (!stack.empty()) {
        const Cell cur = stack.back();
        std::vector<Dir> opts;
        for (Dir d : DLIST) { const int nc = cur.c + d.dx * 2, nr = cur.r + d.dy * 2; if (nc > 0 && nr > 0 && nc < COLS - 1 && nr < ROWS - 1 && grid[nr][nc] == WALL) opts.push_back(d); }
        if (opts.empty()) { stack.pop_back(); continue; }
        const Dir d = opts[(size_t)std::floor(rnd() * opts.size())];
        grid[cur.r + d.dy][cur.c + d.dx] = PATH; grid[cur.r + d.dy * 2][cur.c + d.dx * 2] = PATH;
        stack.push_back({cur.c + d.dx * 2, cur.r + d.dy * 2, 0});
    }
    for (int r = 1; r < ROWS; r += 2) for (int c = 1; c < COLS; c += 2) {
        int n = 0; std::vector<Dir> walls;
        for (Dir d : DLIST) { if (grid[r + d.dy][c + d.dx] == PATH) n++; else walls.push_back(d); }
        std::vector<Dir> w2;
        for (Dir d : walls) { const int nc = c + d.dx * 2, nr = r + d.dy * 2; if (nc > 0 && nr > 0 && nc < COLS - 1 && nr < ROWS - 1) w2.push_back(d); }
        if (n == 1 && !w2.empty() && rnd() < 0.9) { const Dir w = w2[(size_t)std::floor(rnd() * w2.size())]; grid[r + w.dy][c + w.dx] = PATH; }
    }
    for (int k = 0; k < 14; k++) {
        const int wc = 1 + (int)std::floor(rnd() * (COLS - 2)), wr = 1 + (int)std::floor(rnd() * (ROWS - 2));
        if ((wc % 2) != (wr % 2) && grid[wr][wc] == WALL) grid[wr][wc] = PATH;
    }
    // a few walls of hardened firmware that can't be rotted
    for (int k = 0; k < 10; k++) {
        const int hc = 2 + (int)std::floor(rnd() * (COLS - 4)), hr = 2 + (int)std::floor(rnd() * (ROWS - 4));
        if (grid[hr][hc] == WALL) hard[hr][hc] = true;
    }
    startCell = {1, 7, 0};
    nodes.clear();
    auto reach = [] { return bfs({startCell}, [](int, int, int nc, int nr) { return grid[nr][nc] != WALL; }); };
    // sealed vaults (and, in the last sector, the restore points): a cell walled in on every side
    int want = S.vaults + S.restore, tries = 0;
    while (want > 0 && tries++ < 4000) {
        const int vc = 3 + 2 * (int)std::floor(rnd() * ((COLS - 5) / 2.0)), vr = 1 + 2 * (int)std::floor(rnd() * ((ROWS - 2) / 2.0));
        bool near = std::abs(vc - startCell.c) + std::abs(vr - startCell.r) < 6;
        for (const Node& o : nodes) if (std::abs(o.c - vc) + std::abs(o.r - vr) < 4) near = true;
        if (near) continue;
        if (grid[vr][vc] != PATH) continue;
        std::vector<Dir> closed;
        for (Dir d : DLIST) if (grid[vr + d.dy][vc + d.dx] == PATH) { grid[vr + d.dy][vc + d.dx] = WALL; closed.push_back(d); }
        for (Dir d : DLIST) {
            const int hr = vr + d.dy, hc = vc + d.dx;
            hard[hr][hc] = hard[hr][hc] && (hr == 0 || hc == 0 || hr == ROWS - 1 || hc == COLS - 1);
        }
        grid[vr][vc] = WALL;
        Field dd = reach();
        bool okk = true;
        for (int r = 0; r < ROWS && okk; r++) for (int c = 0; c < COLS; c++) if (grid[r][c] == PATH && dd[r][c] < 0 && !nodeAt(c, r)) { okk = false; break; }
        grid[vr][vc] = PATH;
        // a rottable wall must still touch the open maze, or the vault can't be broken into
        bool touch = false;
        for (Dir d : DLIST) {
            const int wc2 = vc + d.dx * 2, wr2 = vr + d.dy * 2;
            if (!hard[vr + d.dy][vc + d.dx] && inside(wc2, wr2) && grid[wr2][wc2] == PATH && dd[wr2][wc2] >= 0) { touch = true; break; }
        }
        if (!okk || !touch) { for (Dir d : closed) grid[vr + d.dy][vc + d.dx] = PATH; continue; }
        const bool isRestore = want <= S.restore;
        nodes.push_back({vc, vr, false, !isRestore, isRestore, false, false, rnd() * 10});
        want--;
    }
    Field dist = reach();
    std::vector<Cell> cells;
    for (int r = 1; r < ROWS - 1; r++) for (int c = 1; c < COLS - 1; c++)
        if (grid[r][c] == PATH && dist[r][c] > 2 && !nodeAt(c, r)) cells.push_back({c, r, 0});
    for (Cell& p : cells) {
        int n = 0;
        for (Dir d : DLIST) if (open(p.c + d.dx, p.r + d.dy)) n++;
        p.j = n + rnd() * 2.5;
    }
    std::stable_sort(cells.begin(), cells.end(), [](const Cell& a, const Cell& b) { return a.j > b.j; });
    const int total = S.nodes + S.honey;
    int placed = 0;
    for (int gap = 4; gap >= 1 && placed < total; gap--) {
        for (size_t k = 0; k < cells.size() && placed < total; k++) {
            const Cell& p = cells[k];
            bool close = false;
            for (const Node& o : nodes) if (std::abs(o.c - p.c) + std::abs(o.r - p.r) < gap) { close = true; break; }
            if (close) continue;
            nodes.push_back({p.c, p.r, false, false, false, false, false, rnd() * 10});
            placed++;
        }
    }
    std::vector<size_t> plain;
    for (size_t i = 0; i < nodes.size(); i++) if (!nodes[i].vault && !nodes[i].restore) plain.push_back(i);
    for (int k = 0; k < S.honey && !plain.empty(); k++) {
        const size_t i = (size_t)std::floor(rnd() * plain.size());
        nodes[plain[i]].honey = true;
        plain.erase(plain.begin() + i);
    }
    avSpawns.clear();
    for (const Cell& p : cells) if (dist[p.r][p.c] >= 14) avSpawns.push_back(p);
    if (avSpawns.empty()) avSpawns.assign(cells.size() > 5 ? cells.end() - 5 : cells.begin(), cells.end());
    zones.clear();
    if (std::string(S.sys) == "quarantine") { zones.push_back({6, 2, 12, 6, 0, 0, 0, -1}); zones.push_back({18, 8, 24, 12, 0, 5, 0, -1}); }
    for (Zone& z : zones) { z.state = 0; z.t = 6 + z.off; z.guard = -1; }
    firewall = Firewall(); firewall.on = std::string(S.sys) == "firewall"; firewall.phase = 0; firewall.t = 4; firewall.x = -1; firewall.dir = 1;
    scanner = Scanner(); scanner.on = std::string(S.sys) == "scanner";
    restore = Restore(); restore.on = std::string(S.sys) == "restore"; restore.t = 14;
}

void loadSector(int i) {
    sector = i;
    build();
    avs.clear(); decoys.clear(); traps.clear(); rotting.clear(); particles.clear();
    const Sector& S = SECTORS[sector];
    Rng rnd((uint32_t)(77 + sector));
    for (int k = 0; k < S.av + cycle; k++) {
        const Cell& sp = avSpawns[(size_t)std::floor(rnd() * avSpawns.size())];
        avs.push_back(makeAV(sp.c, sp.r, false));
    }
    spawnPlayer();
    aBurst.t = aBurst.cd = aCloak.t = aCloak.cd = aSplit.cd = 0;
    energy = std::max(energy, 4.0);
    state = Play; stateT = 0; introT = 4.5; alertT = 0;
    if (sector + cycle * 6 > reached && !practice) { reached = sector + cycle * 6; saveInt("reached.txt", reached); }
    log("> mount " + lower(S.name) + ".sys");
    log(std::string("> ") + S.intro);
    int sealed = 0;
    for (const Node& o : nodes) if (o.vault) sealed++;
    if (sealed) log("> " + std::to_string(sealed) + " vaults sealed in the walls: face one, ROT (SPACE) to break in");
}

// ---- movement on the grid ---------------------------------------------------------------------------------------------
Zone* zoneOf(int c, int r) {
    for (Zone& z : zones) if (c >= z.c0 && c <= z.c1 && r >= z.r0 && r <= z.r1) return &z;
    return nullptr;
}
bool canStep(int c, int r, int nc, int nr) {
    if (!open(nc, nr)) return false;
    Zone *a = zoneOf(c, r), *b = zoneOf(nc, nr);
    if (a != b && ((a && a->state == 2) || (b && b->state == 2))) return false;
    return true;
}
struct Pt { double x, y; };
Pt pos(const Ent& e) { return {e.c + (e.hasDir ? e.dir.dx * e.prog : 0), e.r + (e.hasDir ? e.dir.dy * e.prog : 0)}; }
Cell nearCell(const Ent& e) { return e.hasDir && e.prog >= 0.5 ? Cell{e.c + e.dir.dx, e.r + e.dir.dy, 0} : Cell{e.c, e.r, 0}; }

template <class Arrive>
void advance(Ent& e, double speed, double dt, Arrive arrive) {
    double d = speed * dt;
    int guard = 0;
    if (e.hasDir && e.hasWant && e.want.dx == -e.dir.dx && e.want.dy == -e.dir.dy && e.prog > 0) {
        e.c += e.dir.dx; e.r += e.dir.dy; e.prog = 1 - e.prog; e.dir = e.want;
    }
    if (e.hasDir && e.prog > 0 && !canStep(e.c, e.r, e.c + e.dir.dx, e.r + e.dir.dy) && !open(e.c + e.dir.dx, e.r + e.dir.dy)) {
        e.prog = 0;                                         // the cell ahead turned back into wall: back off
    }
    while (d > 1e-9 && guard++ < 8) {
        if (e.prog == 0) {
            if (e.hasWant && canStep(e.c, e.r, e.c + e.want.dx, e.r + e.want.dy)) { e.dir = e.want; e.hasDir = true; }
            else if (e.hasDir && !canStep(e.c, e.r, e.c + e.dir.dx, e.r + e.dir.dy)) e.hasDir = false;
            if (!e.hasDir) break;
        }
        const double need = 1 - e.prog;
        if (d < need) { e.prog += d; d = 0; }
        else { d -= need; e.prog = 0; e.c += e.dir.dx; e.r += e.dir.dy; arrive(e); }
    }
}

// ---- the player's abilities -----------------------------------------------------------------------------------------------
bool cloaked() { return aCloak.t > 0; }

void doRot() {
    if (state != Play) return;
    const Cell at = nearCell(player);
    const Dir f = player.face;
    const int tc = at.c + f.dx, tr = at.r + f.dy;
    if (!inside(tc, tr) || grid[tr][tc] != WALL) return;
    for (const Rotting& o : rotting) if (o.c == tc && o.r == tr) return;
    if (hard[tr][tc]) { log("> rot " + hex(tr * COLS + tc) + ": ACCESS DENIED (firmware)"); Sfx::denied(); return; }
    if (energy < ROT_COST) { log("> rot: not enough infection energy"); Sfx::denied(); return; }
    energy -= ROT_COST;
    rotting.push_back({tc, tr, 0});
    burst((tc + 0.5f) * CS + OX, (tr + 0.5f) * CS + OY, 30, 2.5f, {C::pink, C::pink, C::black, C::white});
    glitchT = std::max(glitchT, 0.12);
    Sfx::rot();
    log("> rot " + hex(tr * COLS + tc) + " ... corrupted");
}
void doBurst() {
    if (!has("burst") || aBurst.cd > 0 || state != Play) return;
    aBurst.t = 1.6; aBurst.cd = 6; Sfx::burst(); log("> overclock: speed burst");
}
void doTrap() {
    if (!has("trap") || state != Play) return;
    const Cell at = nearCell(player);
    bool there = false;
    for (const Trap& t : traps) if (t.c == at.c && t.r == at.r) there = true;
    if (energy < TRAP_COST || traps.size() >= 3 || there) { Sfx::denied(); return; }
    energy -= TRAP_COST; traps.push_back({at.c, at.r});
    Sfx::trap(); log("> trap planted at " + hex(at.r * COLS + at.c));
}
void doCloak() {
    if (!has("cloak") || aCloak.cd > 0 || state != Play) return;
    aCloak.t = 3.5; aCloak.cd = 12; Sfx::cloak(); log("> disguise: harmless_data.tmp");
}
void doSplit() {
    if (!has("split") || aSplit.cd > 0 || state != Play) return;
    aSplit.cd = 10;
    const Cell at = nearCell(player);
    Ent d; d.c = at.c; d.r = at.r; d.hasWant = true; d.want = {-player.face.dx, -player.face.dy}; d.life = 8; d.face = LEFT; d.id = nextId++;
    decoys.push_back(d);
    Sfx::split(); log("> fork(): copy spawned");
    burst(px(at.c), py(at.r), 20, 2, {C::pink, C::white});
}

// ---- what happens when something arrives in a cell ------------------------------------------------------------------------
int remaining() { int n = 0; for (const Node& o : nodes) if (!o.inf && !o.honey && !o.dead) n++; return n; }
int targetCount() { int n = 0; for (const Node& o : nodes) if (!o.honey && !o.dead) n++; return n; }
void saveBest() { if (score > best && !practice) { best = score; saveInt("best.txt", best); } }

void checkClear() {
    if (remaining() > 0 || state != Play) return;
    state = Clear; stateT = 0;
    const int bonus = 1000 + lives * 250;
    score += bonus;
    if (lives < 5) lives++;
    Sfx::clear();
    log("> SECTOR INFECTED. +" + std::to_string(bonus));
    for (int i = 0; i < 6; i++) burst(OX + randomf() * COLS * CS, OY + randomf() * ROWS * CS, 30, 3, {C::pink, C::white, C::green});
    saveBest();
}

void infectAt(int c, int r, bool byCopy, Ent* who) {
    Node* nd = nullptr;
    for (Node& o : nodes) if (o.c == c && o.r == r) nd = &o;
    if (!nd || nd->inf) return;
    if (nd->honey) {
        nd->honey = false; nd->dead = true;
        burst(px(c), py(r), 50, 3, {C::green, C::white});
        glitchT = 0.4; Sfx::honey();
        log("> HONEYPOT TRIGGERED at " + hex(r * COLS + c));
        Ent a = makeAV(c, r, false); a.stun = 0.8; avs.push_back(a);
        if (byCopy) { who->life = 0; }
        else { player.stun = 1; energy = std::max(0.0, energy - 3); alertT = 4; }
        return;
    }
    nd->inf = true;
    const int gain = nd->vault ? 3 : nd->restore ? 2 : 1;
    energy = std::min((double)MAX_ENERGY, energy + gain);
    score += nd->vault ? 300 : nd->restore ? 500 : 100;
    burst(px(c), py(r), nd->vault || nd->restore ? 46 : 22, 2.4f, {C::pink, C::pink, C::white});
    Sfx::infect();
    if (nd->restore) {
        int left = 0;
        for (const Node& o : nodes) if (o.restore && !o.inf) left++;
        log(left ? "> restore point corrupted: " + std::to_string(left) + " left" : "> RESTORE POINTS GONE: System Restore has failed");
        glitchT = 0.3;
    } else if (nd->vault) log("> vault " + hex(r * COLS + c) + " cracked +3 energy");
    checkClear();
}

void hit() {
    if (god || player.inv > 0 || state != Play) return;
    lives--;
    glitchT = 0.6;
    const Pt p = pos(player);
    burst(px(p.x), py(p.y), 70, 4, {C::pink, C::white, C::green});
    Sfx::die();
    log("> process killed by antivirus. " + (lives > 0 ? std::to_string(lives) + " left" : std::string("no copies left")));
    if (lives <= 0) { state = Over; stateT = 0; saveBest(); return; }
    state = Dying; stateT = 0;
}

// ---- the antivirus ------------------------------------------------------------------------------------------------------
void chooseAV(Ent& e, bool hunting) {
    std::vector<Dir> opts;
    for (Dir k : DLIST) if (canStep(e.c, e.r, e.c + k.dx, e.r + k.dy)) opts.push_back(k);
    if (opts.empty()) { e.hasWant = false; return; }
    if (hunting && hasField) {
        int bestD = 1000000000; const Dir* pick = nullptr;
        for (const Dir& k : opts) { const int v = distField[e.r + k.dy][e.c + k.dx]; if (v >= 0 && v < bestD) { bestD = v; pick = &k; } }
        if (pick && mrand() > 0.08) { e.want = *pick; e.hasWant = true; return; }
    }
    std::vector<Dir> fwd;
    for (Dir k : opts) if (!e.hasDir || k.dx != -e.dir.dx || k.dy != -e.dir.dy) fwd.push_back(k);
    const std::vector<Dir>& list = fwd.empty() ? opts : fwd;
    e.want = list[(size_t)(mrand() * list.size())]; e.hasWant = true;
}

bool rotCell(int c, int r) { return inside(c, r) && grid[r][c] == ROT; }

bool occupied(int c, int r) {
    auto at = [&](const Ent& e) { const Cell n = nearCell(e); return (e.c == c && e.r == r) || (n.c == c && n.r == r); };
    if (at(player)) return true;
    for (const Ent& e : avs) if (at(e)) return true;
    for (const Ent& e : decoys) if (at(e)) return true;
    return false;
}

void systems(double dt, Pt pp) {
    const Cell pc = nearCell(player);
    const bool safe = cloaked() || player.inv > 0 || god;
    if (firewall.on) {                                       // a wall of fire sweeping across the board
        Firewall& f = firewall;
        f.t -= dt;
        if (f.phase == 0 && f.t <= 0) { f.phase = 1; f.t = 1.6; log(std::string("> firewall sweep incoming ") + (f.dir > 0 ? ">>>" : "<<<")); Sfx::alarm(); }
        else if (f.phase == 1 && f.t <= 0) { f.phase = 2; f.x = f.dir > 0 ? -0.5 : COLS - 0.5; Sfx::sweep(); }
        else if (f.phase == 2) {
            f.x += f.dir * (4.2 + cycle * 0.5) * dt;
            traps.erase(std::remove_if(traps.begin(), traps.end(), [&](const Trap& t) { return std::fabs(t.c - f.x) <= 0.5; }), traps.end());
            for (Ent& d : decoys) if (std::fabs(pos(d).x - f.x) < 0.5) d.life = 0;
            if (!safe && std::fabs(pp.x - f.x) < 0.5 && !rotCell(pc.c, pc.r)) hit();
            if (f.x > COLS + 0.5 || f.x < -1) { f.phase = 0; f.t = 5.5; f.dir = -f.dir; }
        }
    }
    for (Zone& z : zones) {                                  // quarantine zones: open, warning, locked
        z.t -= dt;
        if (z.state == 0 && z.t <= 0) { z.state = 1; z.t = 1.8; }
        else if (z.state == 1 && z.t <= 0) {
            z.state = 2; z.t = 5;
            if (zoneOf(pc.c, pc.r) == &z && !god) {
                log("> QUARANTINE: process isolated. guard deployed");
                Sfx::alarm();
                std::vector<Cell> cells;
                for (int r = z.r0; r <= z.r1; r++) for (int c = z.c0; c <= z.c1; c++) if (open(c, r) && std::abs(c - pc.c) + std::abs(r - pc.r) >= 4) cells.push_back({c, r, 0});
                if (!cells.empty()) {
                    const Cell s = cells[(size_t)(mrand() * cells.size())];
                    Ent g = makeAV(s.c, s.r, true); g.stun = 0.6; z.guard = g.id; avs.push_back(g);
                }
            }
        } else if (z.state == 2) {
            if (zoneOf(pc.c, pc.r) == &z) energy = std::max(0.0, energy - dt * 0.6);
            if (z.t <= 0) {
                z.state = 0; z.t = 7;
                if (z.guard >= 0) {
                    for (size_t i = 0; i < avs.size(); i++) if (avs[i].id == z.guard) {
                        const Pt gp = pos(avs[i]);
                        avs.erase(avs.begin() + i);
                        burst(px(gp.x), py(gp.y), 20, 2, {C::green});
                        break;
                    }
                    z.guard = -1;
                }
            }
        }
    }
    if (scanner.on) {                                        // a radar beam sweeping round from the middle
        scanner.a = std::fmod(scanner.a + dt * 1.05, PI * 2);
        if (scanner.cool > 0) scanner.cool -= dt;
        const double ang = std::atan2(pp.y - 7, pp.x - 15);
        const double diff = std::fabs(std::fmod(std::fmod(ang - scanner.a, PI * 2) + PI * 3, PI * 2) - PI);
        if (!safe && diff < 0.09 && scanner.cool <= 0 && !rotCell(pc.c, pc.r)) {
            alertT = 4; scanner.cool = 2; glitchT = 0.2;
            log("> SCANNER: INTRUSION DETECTED at " + hex(pc.r * COLS + pc.c)); Sfx::alarm();
        }
    }
    if (restore.on) {                                        // System Restore: a wave that undoes the corruption
        Restore& R = restore;
        int left = 0;
        for (const Node& o : nodes) if (o.restore && !o.inf) left++;
        if (left == 0) { R.phase = 3; return; }
        const double reach = 6 + left * 5;
        R.t -= dt;
        if (R.phase == 0 && R.t <= 0) { R.phase = 1; R.t = 2.2; log("> System Restore: rolling back to restore point..."); Sfx::restoreS(); }
        else if (R.phase == 1 && R.t <= 0) { R.phase = 2; R.R = 0; R.prevR = 0; }
        else if (R.phase == 2) {
            R.prevR = R.R; R.R += dt * 11;
            int lost = 0;
            for (Node& o : nodes) {
                if (!o.inf || o.restore) continue;
                const double d = std::hypot(o.c - R.cc, o.r - R.cr);
                if (d >= R.prevR && d < R.R && d <= reach) { o.inf = false; lost++; burst(px(o.c), py(o.r), 14, 1.8f, {C::green, C::white}); }
            }
            for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
                if (grid[r][c] != ROT) continue;
                const double d = std::hypot(c - R.cc, r - R.cr);
                if (d >= R.prevR && d < R.R && d <= reach && !occupied(c, r)) grid[r][c] = WALL;
            }
            if (lost) log("> restored " + std::to_string(lost) + " node" + (lost > 1 ? "s" : ""));
            if (R.R > reach) { R.phase = 0; R.t = 13; }
        }
    }
}

// ---- one fixed step of the game -------------------------------------------------------------------------------------------
void step(double dt) {
    timeS += dt;
    if (glitchT > 0) glitchT -= dt;
    if (introT > 0) introT -= dt;
    particles.erase(std::remove_if(particles.begin(), particles.end(), [](Particle& p) {
        p.x += p.vx; p.y += p.vy; p.vx *= 0.94f; p.vy *= 0.94f; return --p.life <= 0; }), particles.end());
    if (state == Dying) {
        stateT += dt;
        if (stateT > 1.2) { spawnPlayer(); for (Ent& a : avs) a.stun = std::max(a.stun, 1.2); state = Play; }
        return;
    }
    if (state != Play) { stateT += dt; return; }

    if (aBurst.t > 0) aBurst.t -= dt;
    if (aCloak.t > 0) aCloak.t -= dt;
    if (aBurst.cd > 0) aBurst.cd -= dt;
    if (aCloak.cd > 0) aCloak.cd -= dt;
    if (aSplit.cd > 0) aSplit.cd -= dt;
    if (player.inv > 0) player.inv -= dt;
    if (energy < 4) energy = std::min(4.0, energy + dt * 0.25);   // a slow trickle, so you can never be stuck
    if (alertT > 0) alertT -= dt;

    rotting.erase(std::remove_if(rotting.begin(), rotting.end(), [&](Rotting& o) {   // walls rotting into pathways
        o.t += dt;
        if (o.t >= 0.35) { grid[o.r][o.c] = ROT; return true; }
        return false;
    }), rotting.end());

    if (player.stun > 0) player.stun -= dt;
    else advance(player, (6 + std::min(cycle, 3) * 0.3) * (aBurst.t > 0 ? 1.75 : 1), dt, [](Ent& e) { infectAt(e.c, e.r, false, nullptr); });

    for (size_t i = 0; i < decoys.size(); i++) {
        decoys[i].life -= dt;
        advance(decoys[i], 5.5, dt, [](Ent& e) {
            infectAt(e.c, e.r, true, &e);
            std::vector<Dir> opts;
            for (Dir k : DLIST) if (canStep(e.c, e.r, e.c + k.dx, e.r + k.dy) && !(e.hasDir && k.dx == -e.dir.dx && k.dy == -e.dir.dy)) opts.push_back(k);
            if (!opts.empty()) { e.want = opts[(size_t)(mrand() * opts.size())]; e.hasWant = true; }
            else if (e.hasDir) { e.want = {-e.dir.dx, -e.dir.dy}; e.hasWant = true; }
            else e.hasWant = false;
        });
        if (decoys[i].life <= 0) { const Pt p = pos(decoys[i]); burst(px(p.x), py(p.y), 14, 1.5f, {C::pink}); }
    }
    decoys.erase(std::remove_if(decoys.begin(), decoys.end(), [](const Ent& d) { return d.life <= 0; }), decoys.end());

    // the antivirus: a distance field from everything they hunt
    std::vector<Cell> lures;
    for (const Ent& d : decoys) lures.push_back(nearCell(d));
    if (!cloaked()) lures.push_back(nearCell(player));
    hasField = !lures.empty();
    if (hasField) distField = bfs(lures, canStep);
    const double avSpeed = 3.9 + sector * 0.2 + cycle * 0.4;
    const int chaseR = 7 + sector;
    for (Ent& a : avs) {
        if (a.stun > 0) { a.stun -= dt; continue; }
        const bool hunting = hasField && (alertT > 0 || (distField[a.r][a.c] >= 0 && distField[a.r][a.c] <= chaseR));
        advance(a, avSpeed * (alertT > 0 ? 1.25 : 1) * (hunting ? 1 : 0.85), dt, [&](Ent& e) {
            for (size_t i = 0; i < traps.size(); i++) if (traps[i].c == e.c && traps[i].r == e.r) {
                e.stun = 5; traps.erase(traps.begin() + i); burst(px(e.c), py(e.r), 30, 2, {C::pink, C::green}); Sfx::snare(); log("> antivirus snared in corruption"); return;
            }
            chooseAV(e, hunting);
        });
        if (a.prog == 0 && !a.hasDir) chooseAV(a, hunting);
        if (a.hasDir) a.face = a.dir;
    }

    // catching: the player, or a copy
    const Pt pp = pos(player);
    for (const Ent& a : avs) {
        if (a.stun > 0) continue;
        const Pt ap = pos(a);
        for (Ent& d : decoys) { const Pt dp = pos(d); if (std::fabs(dp.x - ap.x) + std::fabs(dp.y - ap.y) < 0.7) d.life = 0; }
        if (!cloaked() && std::fabs(pp.x - ap.x) + std::fabs(pp.y - ap.y) < 0.7) hit();
    }
    if (state != Play) return;
    systems(dt, pp);
}

// ---- drawing ----------------------------------------------------------------------------------------------------------
Canvas* cx = nullptr;
#define ctx (*cx)
void glow(ImU32 col, float b) { ctx.shadowColor = col; ctx.shadowBlur = b; ctx.fillStyle = col; ctx.strokeStyle = col; }
void noGlow() { ctx.shadowBlur = 0; }

void drawHex(float x, float y, float r, float rot) {
    ctx.beginPath();
    for (int i = 0; i < 6; i++) {
        const float a = rot + i * (float)PI / 3;
        if (i) ctx.lineTo(x + std::cos(a) * r, y + std::sin(a) * r); else ctx.moveTo(x + std::cos(a) * r, y + std::sin(a) * r);
    }
    ctx.closePath();
}

void drawBoard() {
    int m[ROWS][COLS];                                       // which cells the pink is bleeding into
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) m[r][c] = grid[r][c] == ROT ? 2 : 0;
    for (const Node& o : nodes) {
        if (!o.inf) continue;
        for (int r = o.r - 2; r <= o.r + 2; r++) for (int c = o.c - 2; c <= o.c + 2; c++)
            if (inside(c, r) && std::abs(c - o.c) + std::abs(r - o.r) <= 2) m[r][c] = std::max(m[r][c], 1);
    }
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        const float x = OX + c * CS, y = OY + r * CS;
        if (grid[r][c] == WALL) {
            ctx.fillStyle = hard[r][c] ? rgb("#00253d") : C::navy;
            ctx.fillRect(x + 1, y + 1, CS - 2, CS - 2);
            ctx.fillStyle = hard[r][c] ? C::deep : rgb("#002a44");
            if (hard[r][c]) { for (int k = 0; k < 3; k++) ctx.fillRect(x + 4 + k * 8, y + 4, 2, CS - 8); }
            else { ctx.fillRect(x + 6, y + 6, 3, 3); ctx.fillRect(x + CS - 9, y + CS - 9, 3, 3); }
        } else if (grid[r][c] == ROT) {
            ctx.fillStyle = rgb("#1f0820"); ctx.fillRect(x, y, CS, CS);
            ctx.fillStyle = C::pink;
            Rng seed((uint32_t)(r * 131 + c * 7));
            for (int k = 0; k < 7; k++) {
                ctx.globalAlpha = (float)(0.25 + seed() * 0.4);
                const float a = (float)seed(), b = (float)seed(), w = (float)seed();
                ctx.fillRect(x + a * (CS - 6), y + b * (CS - 3), 2 + w * 8, 2);
            }
            ctx.globalAlpha = 1;
        }
    }
    // the traces: green copper, pink where the infection has bled into them
    for (int r = 0; r < ROWS; r++) for (int c = 0; c < COLS; c++) {
        if (grid[r][c] == WALL) continue;
        const int ds[2][2] = {{1, 0}, {0, 1}};
        for (const auto& d : ds) {
            const int nc = c + d[0], nr = r + d[1];
            if (!open(nc, nr)) continue;
            const bool pink = m[r][c] || m[nr][nc];
            const ImU32 col = pink ? C::pink : C::green;
            ctx.strokeStyle = col; ctx.shadowColor = col; ctx.shadowBlur = 6;
            ctx.globalAlpha = pink ? 0.75f : 0.45f; ctx.lineWidth = 3;
            ctx.beginPath(); ctx.moveTo(px(c), py(r)); ctx.lineTo(px(nc), py(nr)); ctx.stroke();
            ctx.shadowBlur = 0;
            ctx.globalAlpha = 0.2f; ctx.lineWidth = 1;
            ctx.beginPath(); ctx.moveTo(px(c) + d[1] * 6, py(r) + d[0] * 6); ctx.lineTo(px(nc) + d[1] * 6, py(nr) + d[0] * 6); ctx.stroke();
        }
        int n = 0;
        for (Dir d : DLIST) if (open(c + d.dx, r + d.dy)) n++;
        if (n >= 3) {
            ctx.globalAlpha = 0.7f; ctx.fillStyle = m[r][c] ? C::pink : C::green;
            ctx.beginPath(); ctx.arc(px(c), py(r), 3, 0, (float)PI * 2); ctx.fill();
        }
    }
    ctx.globalAlpha = 1; ctx.shadowBlur = 0;
    if (restore.on) {                                        // the restore core, printed on the board
        ctx.strokeStyle = C::green; ctx.globalAlpha = 0.35f; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.arc(px(15), py(7), 13, 0, (float)PI * 2); ctx.stroke();
        ctx.beginPath(); ctx.arc(px(15), py(7), 6, 0, (float)PI * 2); ctx.stroke();
        ctx.globalAlpha = 1;
    }
}

void drawNode(const Node& o) {
    const float x = px(o.c), y = py(o.r), s = o.vault || o.restore ? 8.f : 6.f, t = (float)timeS;
    if (o.dead) return;
    if (o.inf) {
        ctx.globalAlpha = 0.1f + 0.05f * std::sin(t * 3 + (float)o.flick);
        ctx.fillStyle = C::pink; ctx.beginPath(); ctx.arc(x, y, CS * 1.3f, 0, (float)PI * 2); ctx.fill();
        ctx.globalAlpha = 1;
        glow(C::pink, 16 + 6 * std::sin(t * 4 + (float)o.flick));
        ctx.fillRect(x - s, y - s, s * 2, s * 2);
        noGlow(); ctx.fillStyle = C::black; ctx.fillRect(x - s + 3, y - s + 3, s * 2 - 6, s * 2 - 6);
        ctx.fillStyle = C::pink; ctx.fillRect(x - 2, y - 2, 4, 4);
    } else {
        const ImU32 col = o.restore ? C::sky : C::green;
        glow(col, 10);
        ctx.lineWidth = 2; ctx.strokeRect(x - s, y - s, s * 2, s * 2);
        noGlow();
        for (int k = -1; k <= 1; k += 2) { ctx.fillRect(x - s - 4, y + k * 3 - 1, 3, 2); ctx.fillRect(x + s + 1, y + k * 3 - 1, 3, 2); }
        ctx.fillRect(x - 2, y - 2, 4, 4);
        if (o.restore) { ctx.strokeStyle = C::sky; ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(x, y, 13 + 2 * std::sin(t * 3), 0, (float)PI * 2); ctx.stroke(); }
        if (o.vault) {                                       // sealed in on purpose: a pink lock ring, so it reads as a target
            ctx.strokeStyle = C::pink; ctx.lineWidth = 1.5f;
            ctx.setLineDash(4, 3); ctx.lineDashOffset = -t * 8;
            ctx.beginPath(); ctx.arc(x, y, 14 + 1.5f * std::sin(t * 3 + (float)o.flick), 0, (float)PI * 2); ctx.stroke();
            ctx.setLineDash(0, 0); ctx.lineDashOffset = 0;
            ctx.fillStyle = C::pink; ctx.font(9, true); ctx.textAlign = 1; ctx.fillText("VAULT", x, y - 17);
        }
        if (o.honey && std::fmod(timeS + o.flick, 3.2) < 0.18) {
            ctx.fillStyle = C::green; ctx.font(14, true); ctx.textAlign = 1; ctx.fillText("?", x, y - 11);
        }
    }
}

void drawAV(const Ent& a) {
    const Pt p = pos(a);
    const float x = px(p.x), y = py(p.y), t = (float)timeS;
    const bool blinkOn = a.stun > 0 && (int)std::floor(timeS * 10) % 2;
    ctx.globalAlpha = a.stun > 0 ? 0.5f : 1.f;
    glow(C::green, alertT > 0 ? 24.f : 14.f);
    ctx.beginPath();                                         // a shield
    ctx.moveTo(x, y - 11); ctx.lineTo(x + 10, y - 7); ctx.lineTo(x + 8, y + 4); ctx.lineTo(x, y + 11); ctx.lineTo(x - 8, y + 4); ctx.lineTo(x - 10, y - 7); ctx.closePath();
    ctx.fill(); noGlow();
    ctx.fillStyle = C::black;
    ctx.beginPath(); ctx.ellipse(x, y - 1, 6, 3.5f, 0, 0, (float)PI * 2); ctx.fill();
    ctx.fillStyle = blinkOn ? C::black : (alertT > 0 ? C::white : C::green);
    ctx.beginPath(); ctx.arc(x + a.face.dx * 2.5f, y - 1 + a.face.dy * 1.5f, 2.2f, 0, (float)PI * 2); ctx.fill();
    if (a.guard) { ctx.strokeStyle = C::green; ctx.lineWidth = 1; ctx.strokeRect(x - 13, y - 13, 26, 26); }
    if (a.stun > 0) {
        ctx.strokeStyle = C::pink; ctx.lineWidth = 1.5f;
        for (int k = 0; k < 3; k++) { ctx.beginPath(); ctx.arc(x, y, 9.f + k * 3, t * 3 + k, t * 3 + k + 2); ctx.stroke(); }
    }
    ctx.globalAlpha = 1;
}

void drawPlayer(const Ent& e, bool copy) {
    const Pt p = pos(e);
    const float x = px(p.x), y = py(p.y), t = (float)timeS;
    if (!copy && cloaked()) {                               // disguised as a harmless packet of data
        ctx.globalAlpha = 0.55f + 0.2f * std::sin(t * 20);
        ctx.strokeStyle = C::green; ctx.lineWidth = 1; ctx.strokeRect(x - 9, y - 7, 18, 14);
        ctx.fillStyle = C::green; ctx.font(9); ctx.textAlign = 1; ctx.fillText("01", x, y + 3);
        ctx.globalAlpha = 1; return;
    }
    if (!copy && e.inv > 0 && (int)std::floor(timeS * 12) % 2) return;
    ctx.globalAlpha = copy ? 0.55f : 1.f;
    glow(C::pink, copy ? 10.f : 22.f);
    drawHex(x, y, 10, t * (copy ? -2 : 2)); ctx.fill();
    noGlow();
    ctx.fillStyle = C::black; drawHex(x, y, 5, -t * 3); ctx.fill();
    ctx.fillStyle = C::pink;
    for (int i = 0; i < 6; i++) { const float a = t * 2 + i * (float)PI / 3; ctx.fillRect(x + std::cos(a) * 13 - 1, y + std::sin(a) * 13 - 1, 2.5f, 2.5f); }
    if (!copy && aBurst.t > 0) { ctx.globalAlpha = 0.35f; ctx.fillStyle = C::pink; ctx.fillRect(x - e.face.dx * 18 - 4, y - e.face.dy * 18 - 4, 8, 8); }
    ctx.globalAlpha = 1;
}

std::string fixed1(double v) { char b[32]; std::snprintf(b, sizeof b, "%.1f", v); return b; }

void drawSystems() {
    const float t = (float)timeS;
    for (const Zone& z : zones) {
        const float x = OX + z.c0 * CS, y = OY + z.r0 * CS, w = (z.c1 - z.c0 + 1) * CS, h = (z.r1 - z.r0 + 1) * CS;
        if (z.state == 2) {
            ctx.fillStyle = C::green; ctx.globalAlpha = 0.08f; ctx.fillRect(x, y, w, h); ctx.globalAlpha = 1;
            glow(C::green, 14); ctx.lineWidth = 3; ctx.strokeRect(x, y, w, h); noGlow();
            ctx.fillStyle = C::green; ctx.font(11); ctx.textAlign = 0;
            ctx.fillText("QUARANTINE " + std::to_string((int)std::ceil(z.t)) + "s", x + 4, y - 4);
        } else {
            const bool flash = z.state == 1 && (int)std::floor(timeS * 8) % 2;
            ctx.strokeStyle = flash ? C::green : C::deep; ctx.lineWidth = 1; ctx.setLineDash(6, 6);
            ctx.strokeRect(x, y, w, h); ctx.setLineDash(0, 0);
            if (z.state == 1) { ctx.fillStyle = C::green; ctx.font(11); ctx.textAlign = 0; ctx.fillText("LOCKDOWN " + fixed1(z.t), x + 4, y - 4); }
        }
    }
    if (scanner.on) {
        const float cx0 = px(15), cy0 = py(7), R = 600, a = (float)scanner.a;
        ctx.fillStyle = C::green; ctx.globalAlpha = 0.09f;
        ctx.beginPath(); ctx.moveTo(cx0, cy0); ctx.arc(cx0, cy0, R, a - 0.5f, a + 0.09f); ctx.closePath(); ctx.fill();
        ctx.globalAlpha = 0.5f; ctx.strokeStyle = C::green; ctx.lineWidth = 2;
        ctx.beginPath(); ctx.moveTo(cx0, cy0); ctx.lineTo(cx0 + std::cos(a) * R, cy0 + std::sin(a) * R); ctx.stroke();
        ctx.globalAlpha = 1;
        glow(C::green, 14); ctx.beginPath(); ctx.arc(cx0, cy0, 6, 0, (float)PI * 2); ctx.fill(); noGlow();
    }
    if (firewall.on && firewall.phase != 0) {
        const Firewall& f = firewall;
        if (f.phase == 1) {
            if ((int)std::floor(timeS * 6) % 2) {
                ctx.fillStyle = C::green; ctx.font(18, true); ctx.textAlign = 1;
                ctx.fillText(f.dir > 0 ? "FIREWALL >>>" : "<<< FIREWALL", f.dir > 0 ? OX + 80 : OX + COLS * CS - 80, OY + ROWS * CS / 2);
            }
        } else {
            const float fx = OX + ((float)f.x + 0.5f) * CS;
            glow(C::green, 30);
            ctx.globalAlpha = 0.85f; ctx.fillRect(fx - 4, OY, 8, ROWS * CS);
            ctx.globalAlpha = 0.5f; ctx.shadowBlur = 0;
            for (float yy = OY; yy < OY + ROWS * CS; yy += 12) {
                const float fl = 6 + 8 * std::fabs(std::sin(yy * 0.3f + t * 20));
                ctx.beginPath(); ctx.moveTo(fx, yy); ctx.lineTo(fx - f.dir * fl, yy + 6); ctx.lineTo(fx, yy + 12); ctx.fill();
            }
            ctx.globalAlpha = 1; noGlow();
        }
    }
    if (restore.on && restore.phase != 3) {
        const float cx2 = px(15), cy2 = py(7);
        const float pulse = restore.phase == 1 ? 0.5f + 0.5f * std::sin(t * 14) : 0.4f;
        glow(C::green, 16); ctx.lineWidth = 2; ctx.globalAlpha = pulse;
        ctx.beginPath(); ctx.arc(cx2, cy2, 13, t * 2, t * 2 + 4.5f); ctx.stroke(); ctx.globalAlpha = 1;
        if (restore.phase == 2) {
            ctx.lineWidth = 6; ctx.globalAlpha = 0.6f;
            ctx.beginPath(); ctx.arc(cx2, cy2, (float)restore.R * CS, 0, (float)PI * 2); ctx.stroke(); ctx.globalAlpha = 1;
        }
        noGlow();
    }
    for (const Trap& tr : traps) {
        const float x = px(tr.c), y = py(tr.r);
        ctx.strokeStyle = C::pink; ctx.lineWidth = 1.5f; ctx.globalAlpha = 0.8f;
        for (int k = 0; k < 4; k++) { const float a = k * (float)PI / 4 + t; ctx.beginPath(); ctx.moveTo(x - std::cos(a) * 11, y - std::sin(a) * 11); ctx.lineTo(x + std::cos(a) * 11, y + std::sin(a) * 11); ctx.stroke(); }
        ctx.beginPath(); ctx.arc(x, y, 6, 0, (float)PI * 2); ctx.stroke(); ctx.globalAlpha = 1;
    }
    for (const Rotting& o : rotting) {
        const float x = OX + o.c * CS, y = OY + o.r * CS, k = (float)(o.t / 0.35);
        ctx.fillStyle = C::pink; ctx.globalAlpha = 0.4f + 0.4f * randomf();
        for (int i = 0; i < 6; i++) ctx.fillRect(x + randomf() * CS * k, y + randomf() * CS, 4, 2 + randomf() * 6);
        ctx.globalAlpha = 1;
    }
}

bool shouting(const std::string& s) {                      // five capitals in a row: an alarm line
    int run = 0;
    for (size_t i = 2; i < s.size(); i++) { run = (s[i] >= 'A' && s[i] <= 'Z') ? run + 1 : 0; if (run >= 5) return true; }
    return false;
}

void drawHUD() {
    const Sector& S = SECTORS[sector];
    ctx.textAlign = 0; ctx.font(16, true);
    ctx.fillStyle = C::green; ctx.fillText("PROTOCOL", 16, 22);
    ctx.fillStyle = C::pink; ctx.fillText(".hex", 16 + ctx.measureText("PROTOCOL"), 22);
    ctx.fillStyle = C::sky; ctx.font(13);
    ctx.fillText("SECTOR " + std::to_string(sector + 1) + (cycle ? "." + std::to_string(cycle + 1) : "") + " " + S.name, 16, 40);
    ctx.fillStyle = C::green;
    ctx.fillText("SCORE " + std::to_string(score), 250, 22); ctx.fillStyle = C::sky; ctx.fillText("BEST " + std::to_string(std::max(best, score)), 250, 40);
    ctx.fillStyle = C::pink;
    ctx.fillText("NODES " + std::to_string(targetCount() - remaining()) + "/" + std::to_string(targetCount()), 390, 22);
    for (int i = 0; i < lives; i++) { drawHex(400.f + i * 16, 36, 6, 0); ctx.fill(); }
    // the energy bar
    ctx.fillStyle = C::sky; ctx.fillText("ENERGY", 520, 22);
    for (int i = 0; i < MAX_ENERGY; i++) {
        ctx.fillStyle = i < (int)std::floor(energy) ? C::pink : C::navy;
        ctx.fillRect(578.f + i * 9, 12, 7, 12);
    }
    // the abilities
    float x = 520;
    for (const Abil& a : ABIL) {
        const std::string id = a.id;
        const bool have = has(id);
        double cd; double on = 0;
        if (id == "rot") cd = energy < ROT_COST ? 1 : 0;
        else if (id == "trap") cd = energy < TRAP_COST ? 1 : 0;
        else if (id == "burst") { cd = aBurst.cd; on = aBurst.t; }
        else if (id == "cloak") { cd = aCloak.cd; on = aCloak.t; }
        else cd = aSplit.cd;
        ctx.font(10);
        ctx.fillStyle = !have ? rgb("#24384a") : cd > 0 ? C::deep : (on > 0 ? C::white : C::green);
        ctx.fillText(have ? std::string(a.key) + ":" + a.name : "LOCKED", x, 40);
        x += id == "rot" ? 76 : id == "burst" ? 84 : 62;
    }
    if (Synth::muted()) { ctx.fillStyle = C::sky; ctx.font(10); ctx.fillText("MUTED", 905, 22); }
    if (alertT > 0) { ctx.fillStyle = C::green; ctx.font(12, true); if ((int)std::floor(timeS * 6) % 2) ctx.fillText("!! DETECTED", 860, 40); }
    // the terminal log
    ctx.font(12);
    for (size_t i = 0; i < logLines.size(); i++) {
        const LogLine& L = logLines[i];
        ctx.globalAlpha = 0.45f + 0.55f * (i + 1) / logLines.size();
        ctx.fillStyle = L.s.find('!') != std::string::npos || shouting(L.s) ? C::pink : C::green;
        const std::string shown = timeS - L.t < 0.6 ? L.s.substr(0, (size_t)std::floor((timeS - L.t) / 0.6 * L.s.size())) : L.s;
        ctx.fillText(shown, 16.f + (i / 2) * 470, H - 26 + (i % 2) * 15);
    }
    ctx.globalAlpha = 1;
}

void centre(const std::string& text, float y, float size, ImU32 col) {
    ctx.textAlign = 1; ctx.font(size, true);
    ctx.fillStyle = col; ctx.fillText(text, W / 2, y);
}

}  // namespace

// ---- the outside ---------------------------------------------------------------------------------------------------------
void Game::init(const char* folder) {
    saveDir = folder;
    best = loadInt("best.txt");
    reached = loadInt("reached.txt");
    Synth::setMuted(loadInt("muted.txt") == 1);
}

void Game::newGame() { practice = false; frozen = false; score = 0; lives = 3; energy = 4; cycle = 0; logLines.clear(); isPaused = false; loadSector(0); }

bool Game::next() {
    if (state == Clear && stateT > 0.8) {
        if (sector + 1 < NSECTORS) loadSector(sector + 1);
        else { state = Won; stateT = 0; saveBest(); }
        return true;
    }
    if (state == Won && stateT > 1) { cycle++; loadSector(0); return true; }
    if (state == Over && stateT > 1) { newGame(); return true; }
    return false;
}

void Game::key(Key k) {
    if (k == Pause) { if (state == Play) isPaused = !isPaused; return; }
    if (k == Mute) { Synth::setMuted(!Synth::muted()); saveInt("muted.txt", Synth::muted() ? 1 : 0); return; }
    if (isPaused) return;
    auto steer = [](Dir d) { if (state == Play) { player.want = d; player.hasWant = true; player.face = d; } };
    switch (k) {
    case Up: steer(UP); break;
    case Down: steer(DOWN); break;
    case Left: steer(LEFT); break;
    case Right: steer(RIGHT); break;
    case Rot: doRot(); break;
    case Burst: doBurst(); break;
    case Trap: doTrap(); break;
    case Cloak: doCloak(); break;
    case Split: doSplit(); break;
    default: break;
    }
}

void Game::update(double dt) { if (!isPaused && !frozen) step(dt); }

void Game::load(int i) { newGame(); practice = true; logLines.clear(); loadSector(std::clamp(i, 0, NSECTORS - 1)); }

void Game::shot() {                                         // a good-looking moment for the README (index.html#shot)
    newGame();
    practice = true;
    loadSector(3); introT = 0;
    Rng r0(5);
    for (Node& o : nodes) if (r0() < 0.55 && !o.vault) o.inf = true;
    for (int k = 0; k < 9; k++) {
        const int rc = 2 + (int)std::floor(r0() * 27), rr = 1 + (int)std::floor(r0() * 13);
        if (grid[rr][rc] == WALL && !hard[rr][rc]) grid[rr][rc] = ROT;
    }
    player.c = 13; player.r = 7; player.hasDir = false; player.inv = 0;
    while (!open(player.c, player.r)) player.c++;
    scanner.a = 2.3; alertT = 0; energy = 9; score = 4200;
    traps.push_back({player.c + 2, player.r});
    for (int s = 0; s < 40; s++) step(STEP);
    log("> rot 0x00F3 ... corrupted"); log("> SCANNER: INTRUSION DETECTED at 0x00E2");
    timeS = 3;
    frozen = true;
}
bool Game::paused() { return isPaused; }
void Game::setPaused(bool p) { if (state == Play || !p) isPaused = p; }
bool Game::playing() { return state == Play || state == Dying; }

void Game::draw(Canvas& c) {
    cx = &c;
    ctx.fillStyle = C::black; ctx.fillRect(0, 0, W, H);
    drawBoard();
    drawSystems();
    for (const Node& o : nodes) drawNode(o);
    for (const Ent& d : decoys) drawPlayer(d, true);
    for (const Ent& a : avs) drawAV(a);
    if (state != Dying && state != Over) drawPlayer(player, false);
    for (const Particle& p : particles) {
        ctx.globalAlpha = p.life / p.max; glow(p.c, 8); ctx.fillRect(p.x - 1.5f, p.y - 1.5f, 3, 3);
    }
    ctx.globalAlpha = 1; noGlow();
    // scanlines, like an old terminal
    ctx.fillStyle = C::black; ctx.globalAlpha = 0.18f;
    for (float y = 0; y < H; y += 3) ctx.fillRect(0, y, W, 1);
    ctx.globalAlpha = 1;
    drawHUD();

    const Sector& S = SECTORS[sector];
    if (introT > 0 && state == Play) {
        ctx.globalAlpha = (float)std::min(1.0, introT / 0.6);
        ctx.fillStyle = C::black; ctx.fillRect(0, H / 2 - 64, W, 118);
        centre("SECTOR " + std::to_string(sector + 1) + ": " + S.name, H / 2 - 24, 30, C::green);
        ctx.fillStyle = C::white; ctx.font(15); ctx.fillText(S.intro, W / 2, H / 2 + 6);
        ctx.fillStyle = C::pink; ctx.fillText(S.tip, W / 2, H / 2 + 30);
        ctx.globalAlpha = 1;
    }
    auto dim = [] { ctx.fillStyle = C::black; ctx.globalAlpha = 0.7f; ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1; };
    if (isPaused) { dim(); centre("PAUSED", H / 2 - 10, 54, C::green); ctx.fillStyle = C::white; ctx.font(16); ctx.fillText("P to resume", W / 2, H / 2 + 26); }
    else if (state == Clear) {
        dim(); centre("SECTOR INFECTED", H / 2 - 20, 46, C::pink);
        ctx.fillStyle = C::white; ctx.font(16);
        ctx.fillText(stateT > 0.8 ? "Any key or click: next sector" : "", W / 2, H / 2 + 20);
    } else if (state == Over) {
        dim(); centre("PROCESS TERMINATED", H / 2 - 30, 44, C::green);
        ctx.fillStyle = C::white; ctx.font(17);
        ctx.fillText("Score " + std::to_string(score) + "   Best " + std::to_string(best), W / 2, H / 2 + 8);
        ctx.fillStyle = C::pink; ctx.fillText(stateT > 1 ? "Any key or click to reinfect" : "", W / 2, H / 2 + 40);
    } else if (state == Won) {
        dim(); centre("SYSTEM COMPROMISED", H / 2 - 40, 46, C::pink);
        ctx.fillStyle = C::white; ctx.font(17);
        ctx.fillText("Every sector is yours. Score " + std::to_string(score), W / 2, H / 2);
        ctx.fillStyle = C::green; ctx.fillText(stateT > 1 ? "Any key: the system reboots, harder" : "", W / 2, H / 2 + 34);
    }
    if (glitchT > 0) {                                      // the curse: the picture tears
        ctx.tear(6, 40, randomf);
        ctx.fillStyle = C::pink; ctx.globalAlpha = 0.08f; ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1;
    }
}
