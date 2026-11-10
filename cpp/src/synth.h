#pragma once
// synth.h - the sound effects, made here as the browser game makes them with Web Audio (no audio files): oscillators
// whose pitch and volume glide exponentially, and filtered noise bursts, mixed through SDL's audio callback.
namespace Synth {
    enum Wave { Sine, Square, Saw, Triangle };
    enum Filter { Lowpass, Bandpass, Highpass };

    bool   open(float master);              // false: no sound device (the game still runs)
    void   close();
    void   setMuted(bool m);
    bool   muted();
    double now();                           // seconds of sound played so far, like an AudioContext's currentTime

    // a tone: f0 -> f1 Hz and vol -> floor over len seconds, starting at time at (from now());
    // attack > 0: rises from silence over attack seconds first. vibRate / vibDepth: a wobble in Hz.
    void tone(Wave w, float f0, float f1, double at, float len, float vol, float attack = 0.f, float floor = 0.0001f,
              float vibRate = 0.f, float vibDepth = 0.f);
    // a noise burst, fading out ((1 - t)^fadePow), through a filter at freq Hz
    void noise(double at, float len, float vol, float freq, Filter f = Lowpass, int fadePow = 1);
}
