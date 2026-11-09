// PROTOCOL.hex's sound effects, all made with the Web Audio API (no files). The browser only allows sound after a key
// or tap, so the audio wakes on the first one. M mutes.
var Sfx = (function () {
  "use strict";
  var ctx = null, out = null, muted = localStorage.getItem("protocolHexMuted") === "1";

  function wake() {
    if (!ctx) {
      var AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) return;
      ctx = new AC();
      out = ctx.createGain();
      out.gain.value = muted ? 0 : 0.3;
      out.connect(ctx.destination);
    }
    if (ctx.state === "suspended") ctx.resume();
  }
  ["keydown", "pointerdown", "touchstart"].forEach(function (e) { document.addEventListener(e, wake, true); });

  function tone(type, f0, f1, at, len, vol) {
    var o = ctx.createOscillator(), g = ctx.createGain();
    o.type = type;
    o.frequency.setValueAtTime(f0, at);
    o.frequency.exponentialRampToValueAtTime(f1, at + len);
    g.gain.setValueAtTime(vol, at);
    g.gain.exponentialRampToValueAtTime(0.0001, at + len);
    o.connect(g); g.connect(out);
    o.start(at); o.stop(at + len + 0.03);
  }
  function noise(at, len, vol, freq) {
    var n = Math.floor(ctx.sampleRate * len), buf = ctx.createBuffer(1, n, ctx.sampleRate), d = buf.getChannelData(0);
    for (var i = 0; i < n; i++) d[i] = (Math.random() * 2 - 1) * (1 - i / n);
    var s = ctx.createBufferSource(), g = ctx.createGain(), f = ctx.createBiquadFilter();
    f.type = "bandpass"; f.frequency.value = freq || 1500;
    s.buffer = buf; g.gain.value = vol;
    s.connect(f); f.connect(g); g.connect(out);
    s.start(at);
  }
  function ok() { return ctx && !muted; }
  var lastInfect = 0, chain = 0;

  return {
    infect: function () {                 // a bitcrushed blip that climbs on a streak
      if (!ok()) return; var t = ctx.currentTime;
      chain = t - lastInfect < 1.2 ? Math.min(chain + 1, 8) : 0; lastInfect = t;
      var f = 440 * Math.pow(1.122, chain);
      tone("square", f, f * 1.5, t, 0.08, 0.12); tone("square", f * 2, f * 2, t + 0.06, 0.08, 0.07);
    },
    rot: function () { if (!ok()) return; var t = ctx.currentTime; noise(t, 0.35, 0.35, 600); tone("sawtooth", 180, 60, t, 0.35, 0.1); },
    denied: function () { if (!ok()) return; var t = ctx.currentTime; tone("square", 140, 140, t, 0.1, 0.12); tone("square", 140, 140, t + 0.14, 0.1, 0.12); },
    trap: function () { if (!ok()) return; var t = ctx.currentTime; tone("triangle", 600, 200, t, 0.2, 0.2); },
    snare: function () { if (!ok()) return; var t = ctx.currentTime; noise(t, 0.2, 0.3, 3000); tone("square", 900, 300, t, 0.25, 0.1); },
    burst: function () { if (!ok()) return; var t = ctx.currentTime; tone("sawtooth", 200, 1200, t, 0.25, 0.08); },
    cloak: function () { if (!ok()) return; var t = ctx.currentTime; tone("sine", 900, 300, t, 0.4, 0.2); tone("sine", 1350, 450, t, 0.4, 0.1); },
    split: function () { if (!ok()) return; var t = ctx.currentTime; tone("square", 500, 500, t, 0.06, 0.1); tone("square", 750, 750, t + 0.07, 0.06, 0.1); tone("square", 1000, 1000, t + 0.14, 0.06, 0.1); },
    alarm: function () { if (!ok()) return; var t = ctx.currentTime; for (var i = 0; i < 3; i++) tone("square", 880, 660, t + i * 0.18, 0.16, 0.09); },
    honey: function () { if (!ok()) return; var t = ctx.currentTime; tone("sawtooth", 300, 80, t, 0.6, 0.15); noise(t, 0.4, 0.3, 800); },
    sweep: function () { if (!ok()) return; var t = ctx.currentTime; noise(t, 0.8, 0.15, 400); },
    restore: function () { if (!ok()) return; var t = ctx.currentTime; tone("sine", 220, 880, t, 1.2, 0.18); tone("sine", 330, 1320, t, 1.2, 0.08); },
    die: function () { if (!ok()) return; var t = ctx.currentTime; noise(t, 0.6, 0.45, 1200); tone("square", 600, 40, t, 0.7, 0.18); },
    clear: function () {
      if (!ok()) return; var t = ctx.currentTime;
      [392, 523.3, 659.3, 784, 1046.5].forEach(function (f, i) { tone("square", f, f, t + i * 0.08, 0.15, 0.08); tone("triangle", f, f, t + i * 0.08, 0.4, 0.15); });
    },
    toggle: function () {
      muted = !muted;
      localStorage.setItem("protocolHexMuted", muted ? "1" : "0");
      if (out) out.gain.value = muted ? 0 : 0.3;
      return muted;
    },
    isMuted: function () { return muted; }
  };
})();
