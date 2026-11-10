// synth.cpp - the sound effects (see synth.h): a handful of voices mixed in SDL's audio callback.
#include "synth.h"
#include <SDL.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <vector>

namespace {
constexpr int RATE = 44100;
constexpr int MAXV = 48;
constexpr double TAU = 6.283185307179586;

struct Voice {
    bool noise = false;
    int wave = 0, filter = 0, fadePow = 1;
    double start = 0;                    // in samples
    double len = 0, attack = 0;          // in samples
    float f0 = 440, f1 = 440, vol = 0.1f, floor = 0.0001f, vibRate = 0, vibDepth = 0;
    double phase = 0, vibPhase = 0;
    // a biquad (noise)
    float b0 = 0, b1 = 0, b2 = 0, a1 = 0, a2 = 0, x1 = 0, x2 = 0, y1 = 0, y2 = 0;
};

SDL_AudioDeviceID dev = 0;
std::vector<Voice> voices;               // touched only with the device locked
std::atomic<long long> played{0};        // samples
float master = 0.3f;
std::atomic<bool> silent{false};
unsigned seed = 22222;

float rnd() { seed = seed * 1664525u + 1013904223u; return (seed >> 8) / 16777216.f; }

float polyblep(double t, double dt) {
    if (t < dt) { t /= dt; return (float)(t + t - t * t - 1.0); }
    if (t > 1.0 - dt) { t = (t - 1.0) / dt; return (float)(t * t + t + t + 1.0); }
    return 0.f;
}

float sample(Voice& v, double n) {        // n: samples since the voice started
    if (v.noise) {
        const double t = n / v.len;
        const float env = (float)std::pow(1.0 - t, v.fadePow);
        const float x = (rnd() * 2.f - 1.f) * env;
        const float y = v.b0 * x + v.b1 * v.x1 + v.b2 * v.x2 - v.a1 * v.y1 - v.a2 * v.y2;
        v.x2 = v.x1; v.x1 = x; v.y2 = v.y1; v.y1 = y;
        return y * v.vol;
    }
    // pitch: exponential f0 -> f1 over len, then held
    const double tf = std::min(1.0, n / v.len);
    double f = v.f0 * std::pow((double)v.f1 / v.f0, tf);
    if (v.vibDepth > 0.f) { v.vibPhase += v.vibRate / RATE; f += v.vibDepth * std::sin(v.vibPhase * TAU); }
    // volume: (attack: floor -> vol), then vol -> floor, exponentially
    double g;
    if (v.attack > 0 && n < v.attack) g = v.floor * std::pow((double)v.vol / v.floor, n / v.attack);
    else {
        const double a = v.attack > 0 ? v.attack : 0, span = std::max(1.0, v.len - a);
        g = n >= v.len ? v.floor : v.vol * std::pow((double)v.floor / v.vol, (n - a) / span);
    }
    const double dt = std::max(1e-6, f / RATE);
    v.phase += dt;
    v.phase -= std::floor(v.phase);
    const double p = v.phase;
    float s;
    switch (v.wave) {
    case Synth::Square: s = (p < 0.5 ? 1.f : -1.f) + polyblep(p, dt) - polyblep(std::fmod(p + 0.5, 1.0), dt); break;
    case Synth::Saw:    s = (float)(2.0 * p - 1.0) - polyblep(p, dt); break;
    case Synth::Triangle: s = (float)(p < 0.5 ? 4.0 * p - 1.0 : 3.0 - 4.0 * p); break;
    default:            s = (float)std::sin(p * TAU); break;
    }
    return s * (float)g;
}

void callback(void*, Uint8* stream, int len) {
    float* out = (float*)stream;
    const int frames = len / (int)(2 * sizeof(float));
    const long long t0 = played.load();
    for (int i = 0; i < frames; ++i) {
        const double now = (double)(t0 + i);
        float mix = 0.f;
        for (Voice& v : voices) {
            const double n = now - v.start;
            if (n < 0) continue;
            mix += sample(v, n);
        }
        mix = silent ? 0.f : std::tanh(mix * master * 1.1f);
        out[2 * i] = out[2 * i + 1] = mix;
    }
    played.store(t0 + frames);
    // the finished voices go
    const double now = (double)(t0 + frames);
    voices.erase(std::remove_if(voices.begin(), voices.end(),
                                [&](const Voice& v) { return now - v.start > v.len + RATE * 0.05; }), voices.end());
}

void add(Voice v) {
    if (!dev || silent) return;
    SDL_LockAudioDevice(dev);
    const double nowS = (double)played.load();
    if (v.start < nowS) v.start = nowS;
    if ((int)voices.size() >= MAXV) voices.erase(voices.begin());
    voices.push_back(v);
    SDL_UnlockAudioDevice(dev);
}
}  // namespace

bool Synth::open(float m) {
    master = m;
    SDL_AudioSpec want{}, have{};
    want.freq = RATE;
    want.format = AUDIO_F32SYS;
    want.channels = 2;
    want.samples = 512;
    want.callback = callback;
    dev = SDL_OpenAudioDevice(nullptr, 0, &want, &have, 0);
    if (!dev) return false;
    voices.reserve(MAXV);
    SDL_PauseAudioDevice(dev, 0);
    return true;
}

void Synth::close() { if (dev) SDL_CloseAudioDevice(dev); dev = 0; }
void Synth::setMuted(bool m) { silent = m; }
bool Synth::muted() { return silent; }
double Synth::now() { return played.load() / (double)RATE; }

void Synth::tone(Wave w, float f0, float f1, double at, float len, float vol, float attack, float floor,
                 float vibRate, float vibDepth) {
    Voice v;
    v.wave = w;
    v.f0 = std::max(1.f, f0); v.f1 = std::max(1.f, f1);
    v.start = at * RATE; v.len = std::max(0.001f, len) * RATE; v.attack = attack * RATE;
    v.vol = std::max(vol, floor * 1.01f); v.floor = floor;
    v.vibRate = vibRate; v.vibDepth = vibDepth;
    add(v);
}

void Synth::noise(double at, float len, float vol, float freq, Filter f, int fadePow) {
    Voice v;
    v.noise = true;
    v.start = at * RATE; v.len = std::max(0.001f, len) * RATE;
    v.vol = vol; v.fadePow = fadePow;
    // RBJ cookbook biquad, Q = 1 (Web Audio's default)
    const double w0 = TAU * std::min((double)freq, RATE * 0.45) / RATE, cs = std::cos(w0), al = std::sin(w0) / 2.0;
    double b0, b1, b2, a0 = 1 + al, a1 = -2 * cs, a2 = 1 - al;
    if (f == Bandpass) { b0 = al; b1 = 0; b2 = -al; }
    else if (f == Highpass) { b0 = (1 + cs) / 2; b1 = -(1 + cs); b2 = (1 + cs) / 2; }
    else { b0 = (1 - cs) / 2; b1 = 1 - cs; b2 = (1 - cs) / 2; }
    v.b0 = (float)(b0 / a0); v.b1 = (float)(b1 / a0); v.b2 = (float)(b2 / a0);
    v.a1 = (float)(a1 / a0); v.a2 = (float)(a2 / a0);
    add(v);
}
