// PROTOCOL.hex - an Arcade Phantasm game.
// You are a virus loose inside a computer. Slide along the glowing pathways of the board, infect every node (they
// turn pink and become yours) and keep away from the green antivirus. Infection energy lets you corrupt the system
// itself: rot a wall into a new pathway, break into sealed vaults, cut a shortcut or escape. Each sector brings a new
// security system: a firewall, quarantine zones, a scanner, honeypots and, last, System Restore.
(function () {
  "use strict";
  var W = 960, H = 540, CS = 30, COLS = 31, ROWS = 15, OX = 15, OY = 50;
  var C = { navy: "#001725", deep: "#005184", blue: "#007ab7", sky: "#34a9e3", pink: "#f088e9", green: "#39ff6a",
            black: "#020611", white: "#ffffff", dim: "#0b3a1c" };
  var DIR = { up: { dx: 0, dy: -1 }, down: { dx: 0, dy: 1 }, left: { dx: -1, dy: 0 }, right: { dx: 1, dy: 0 } };
  var DLIST = [DIR.up, DIR.right, DIR.down, DIR.left];
  var STEP = 1 / 60;
  var WALL = 1, PATH = 0, ROT = 2;
  var ROT_COST = 2, TRAP_COST = 3, MAX_ENERGY = 12;

  var SECTORS = [
    { name: "BOOT SECTOR", sys: "antivirus", av: 2, nodes: 16, vaults: 2, unlock: "rot",
      intro: "Infect every node. Green antivirus hunt you down the traces.", tip: "SPACE rots the wall ahead into a new path (2 energy). Vaults hide fat nodes." },
    { name: "FIREWALL", sys: "firewall", av: 2, nodes: 18, vaults: 2, unlock: "burst",
      intro: "A firewall sweeps the board. It can't see into your rot: hide in a rotted cell.", tip: "SHIFT: speed burst." },
    { name: "QUARANTINE", sys: "quarantine", av: 3, nodes: 18, vaults: 2, unlock: "trap",
      intro: "Quarantine zones seal shut. Get locked in and a guard spawns beside you.", tip: "C: drop a corruption trap that snares antivirus (3 energy)." },
    { name: "SCANNER", sys: "scanner", av: 3, nodes: 18, vaults: 3, unlock: "cloak",
      intro: "The scanner's beam reveals you to every antivirus at once.", tip: "Q: cloak as harmless data. Nothing sees you." },
    { name: "HONEYPOT", sys: "honeypot", av: 3, nodes: 18, vaults: 2, honey: 4, unlock: "split",
      intro: "Some nodes are honeypots. Watch for the flicker of a '?'.", tip: "F: split off a copy. It infects what it touches, and draws fire." },
    { name: "SYSTEM RESTORE", sys: "restore", av: 4, nodes: 18, vaults: 0, restore: 4, unlock: null,
      intro: "System Restore undoes your corruption, wave after wave.", tip: "Rot into the four restore points and infect them to break it." }
  ];
  var ABIL = [
    { id: "rot", key: "SPACE", name: "ROT" }, { id: "burst", key: "SHIFT", name: "BURST" }, { id: "trap", key: "C", name: "TRAP" },
    { id: "cloak", key: "Q", name: "CLOAK" }, { id: "split", key: "F", name: "SPLIT" }
  ];

  var canvas = document.getElementById("game");
  var ctx = canvas.getContext("2d");
  canvas.width = W; canvas.height = H;
  var board = document.createElement("canvas");
  board.width = W; board.height = H;
  var splash = document.getElementById("splash");
  var shot = location.hash === "#shot";
  var started = false, manual = false, god = false;
  var best = parseInt(localStorage.getItem("protocolHexBest") || "0", 10) || 0;
  var reached = parseInt(localStorage.getItem("protocolHexReached") || "0", 10) || 0;

  var sector = 0, cycle = 0, score = 0, lives = 3, energy = 4, state = "splash", stateT = 0, paused = false;
  var grid, hard, nodes, avs, decoys, traps, rotting, zones, firewall, scanner, restore, player, startCell, avSpawns;
  var particles = [], logLines = [], glitchT = 0, dirty = true, introT = 0, alertT = 0, time = 0, distField = null;
  var abil = { burst: { t: 0, cd: 0 }, cloak: { t: 0, cd: 0 }, split: { cd: 0 } };

  function rng(seed) {
    return function () {
      seed |= 0; seed = seed + 0x6D2B79F5 | 0;
      var t = Math.imul(seed ^ seed >>> 15, 1 | seed);
      t = t + Math.imul(t ^ t >>> 7, 61 | t) ^ t;
      return ((t ^ t >>> 14) >>> 0) / 4294967296;
    };
  }
  function has(id) { var i = ["rot", "burst", "trap", "cloak", "split"].indexOf(id); return cycle > 0 || i <= sector; }
  function inside(c, r) { return c >= 0 && r >= 0 && c < COLS && r < ROWS; }
  function open(c, r) { return inside(c, r) && grid[r][c] !== WALL; }
  function log(s) { if (s.length > 68) s = s.slice(0, 66) + "..";
    logLines.push({ s: s, t: time }); if (logLines.length > 4) logLines.shift(); }
  function hex(n) { return "0x" + ("000" + n.toString(16).toUpperCase()).slice(-4); }

  // ---- building a sector --------------------------------------------------------------------------------------------
  function bfs(fromList, passFn) {
    var d = [], q = [], i;
    for (var r = 0; r < ROWS; r++) { d.push([]); for (var c = 0; c < COLS; c++) d[r].push(-1); }
    fromList.forEach(function (p) { if (inside(p.c, p.r)) { d[p.r][p.c] = 0; q.push(p); } });
    for (i = 0; i < q.length; i++) {
      var p = q[i];
      for (var k = 0; k < 4; k++) {
        var nc = p.c + DLIST[k].dx, nr = p.r + DLIST[k].dy;
        if (inside(nc, nr) && d[nr][nc] < 0 && passFn(p.c, p.r, nc, nr)) { d[nr][nc] = d[p.r][p.c] + 1; q.push({ c: nc, r: nr }); }
      }
    }
    return d;
  }

  function build() {
    var S = SECTORS[sector], rnd = rng(1009 + sector * 7919 + cycle * 104729), r, c, k;
    grid = []; hard = [];
    for (r = 0; r < ROWS; r++) { grid.push([]); hard.push([]); for (c = 0; c < COLS; c++) { grid[r].push(WALL); hard[r].push(r === 0 || c === 0 || r === ROWS - 1 || c === COLS - 1); } }
    // a maze through the odd cells, then braided so there are loops to run round
    var stack = [{ c: 1, r: 1 }]; grid[1][1] = PATH;
    while (stack.length) {
      var cur = stack[stack.length - 1], opts = [];
      DLIST.forEach(function (d) { var nc = cur.c + d.dx * 2, nr = cur.r + d.dy * 2; if (nc > 0 && nr > 0 && nc < COLS - 1 && nr < ROWS - 1 && grid[nr][nc] === WALL) opts.push(d); });
      if (!opts.length) { stack.pop(); continue; }
      var d = opts[Math.floor(rnd() * opts.length)];
      grid[cur.r + d.dy][cur.c + d.dx] = PATH; grid[cur.r + d.dy * 2][cur.c + d.dx * 2] = PATH;
      stack.push({ c: cur.c + d.dx * 2, r: cur.r + d.dy * 2 });
    }
    for (r = 1; r < ROWS; r += 2) for (c = 1; c < COLS; c += 2) {
      var n = 0, walls = [];
      DLIST.forEach(function (d) { if (grid[r + d.dy][c + d.dx] === PATH) n++; else walls.push(d); });
      walls = walls.filter(function (d) { var nc = c + d.dx * 2, nr = r + d.dy * 2; return nc > 0 && nr > 0 && nc < COLS - 1 && nr < ROWS - 1; });
      if (n === 1 && walls.length && rnd() < 0.9) { var w = walls[Math.floor(rnd() * walls.length)]; grid[r + w.dy][c + w.dx] = PATH; }
    }
    for (k = 0; k < 14; k++) {
      var wc = 1 + Math.floor(rnd() * (COLS - 2)), wr = 1 + Math.floor(rnd() * (ROWS - 2));
      if ((wc % 2) !== (wr % 2) && grid[wr][wc] === WALL) grid[wr][wc] = PATH;
    }
    // a few walls of hardened firmware that can't be rotted
    for (k = 0; k < 10; k++) {
      var hc = 2 + Math.floor(rnd() * (COLS - 4)), hr = 2 + Math.floor(rnd() * (ROWS - 4));
      if (grid[hr][hc] === WALL) hard[hr][hc] = true;
    }
    startCell = { c: 1, r: 7 };
    nodes = [];
    var reach = function () { return bfs([startCell], function (a, b, nc, nr) { return grid[nr][nc] !== WALL; }); };
    // sealed vaults (and, in the last sector, the restore points): a cell walled in on every side
    var want = (S.vaults || 0) + (S.restore || 0), tries = 0;
    while (want > 0 && tries++ < 4000) {
      var vc = 3 + 2 * Math.floor(rnd() * ((COLS - 5) / 2)), vr = 1 + 2 * Math.floor(rnd() * ((ROWS - 2) / 2));
      if (Math.abs(vc - startCell.c) + Math.abs(vr - startCell.r) < 6 || nodes.some(function (o) { return Math.abs(o.c - vc) + Math.abs(o.r - vr) < 4; })) continue;
      if (grid[vr][vc] !== PATH) continue;
      var closed = [];
      DLIST.forEach(function (d) { if (grid[vr + d.dy][vc + d.dx] === PATH) { grid[vr + d.dy][vc + d.dx] = WALL; closed.push(d); } });
      DLIST.forEach(function (d) { hard[vr + d.dy][vc + d.dx] = hard[vr + d.dy][vc + d.dx] && (vr + d.dy === 0 || vc + d.dx === 0 || vr + d.dy === ROWS - 1 || vc + d.dx === COLS - 1); });
      grid[vr][vc] = WALL;
      var dd = reach(), okk = true;
      for (r = 0; r < ROWS && okk; r++) for (c = 0; c < COLS; c++) if (grid[r][c] === PATH && dd[r][c] < 0 && !nodes.some(function (o) { return o.c === c && o.r === r; })) { okk = false; break; }
      grid[vr][vc] = PATH;
      // a rottable wall must still touch the open maze, or the vault can't be broken into
      var touch = DLIST.some(function (d) { var wc2 = vc + d.dx * 2, wr2 = vr + d.dy * 2; return !hard[vr + d.dy][vc + d.dx] && inside(wc2, wr2) && grid[wr2][wc2] === PATH && dd[wr2][wc2] >= 0; });
      if (!okk || !touch) { closed.forEach(function (d) { grid[vr + d.dy][vc + d.dx] = PATH; }); continue; }
      var isRestore = want <= (S.restore || 0);
      nodes.push({ c: vc, r: vr, inf: false, vault: !isRestore, restore: isRestore, honey: false, flick: rnd() * 10 });
      want--;
    }
    var dist = reach(), cells = [];
    for (r = 1; r < ROWS - 1; r++) for (c = 1; c < COLS - 1; c++)
      if (grid[r][c] === PATH && dist[r][c] > 2 && !nodes.some(function (o) { return o.c === c && o.r === r; })) cells.push({ c: c, r: r, j: 0 });
    cells.forEach(function (p) { p.j = DLIST.filter(function (d) { return open(p.c + d.dx, p.r + d.dy); }).length + rnd() * 2.5; });
    cells.sort(function (a, b) { return b.j - a.j; });
    var total = S.nodes + (S.honey || 0), placed = 0;
    for (var gap = 4; gap >= 1 && placed < total; gap--) {
      for (k = 0; k < cells.length && placed < total; k++) {
        var p = cells[k];
        if (nodes.some(function (o) { return Math.abs(o.c - p.c) + Math.abs(o.r - p.r) < gap; })) continue;
        nodes.push({ c: p.c, r: p.r, inf: false, vault: false, restore: false, honey: false, flick: rnd() * 10 });
        placed++;
      }
    }
    var plain = nodes.filter(function (o) { return !o.vault && !o.restore; });
    for (k = 0; k < (S.honey || 0) && plain.length; k++) plain.splice(Math.floor(rnd() * plain.length), 1)[0].honey = true;
    avSpawns = cells.filter(function (p) { return dist[p.r][p.c] >= 14; });
    if (!avSpawns.length) avSpawns = cells.slice(-5);
    zones = S.sys === "quarantine" ? [{ c0: 6, r0: 2, c1: 12, r1: 6, t: 0, off: 0 }, { c0: 18, r0: 8, c1: 24, r1: 12, t: 0, off: 5 }] : [];
    zones.forEach(function (z) { z.state = "open"; z.t = 6 + z.off; z.guard = null; });
    firewall = S.sys === "firewall" ? { phase: "wait", t: 4, x: -1, dir: 1 } : null;
    scanner = S.sys === "scanner" ? { a: 0, cool: 0 } : null;
    restore = S.sys === "restore" ? { t: 14, phase: "wait", R: 0, prevR: 0, core: { c: 15, r: 7 } } : null;
  }

  function loadSector(i) {
    sector = i;
    build();
    avs = []; decoys = []; traps = []; rotting = []; particles = [];
    var S = SECTORS[sector], rnd = rng(77 + sector);
    for (var k = 0; k < S.av + cycle; k++) {
      var sp = avSpawns[Math.floor(rnd() * avSpawns.length)];
      avs.push(makeAV(sp.c, sp.r, false));
    }
    spawnPlayer();
    abil.burst.t = abil.burst.cd = abil.cloak.t = abil.cloak.cd = abil.split.cd = 0;
    energy = Math.max(energy, 4);
    dirty = true;
    state = "play"; stateT = 0; introT = 4.5; alertT = 0;
    if (sector + cycle * 6 > reached) { reached = sector + cycle * 6; if (!manual && !shot) localStorage.setItem("protocolHexReached", String(reached)); }
    log("> mount " + S.name.toLowerCase().replace(/ /g, "_") + ".sys");
    log("> " + S.intro);
  }

  function makeAV(c, r, guard) {
    return { c: c, r: r, dir: null, want: null, prog: 0, stun: 0, guard: guard, face: DIR.left, blink: Math.random() * 6 };
  }
  function spawnPlayer() {
    player = { c: startCell.c, r: startCell.r, dir: null, want: null, prog: 0, face: DIR.right, inv: 2, stun: 0 };
  }

  // ---- movement on the grid ---------------------------------------------------------------------------------------
  function zoneOf(c, r) {
    for (var i = 0; i < zones.length; i++) { var z = zones[i]; if (c >= z.c0 && c <= z.c1 && r >= z.r0 && r <= z.r1) return z; }
    return null;
  }
  function canStep(c, r, nc, nr) {
    if (!open(nc, nr)) return false;
    var a = zoneOf(c, r), b = zoneOf(nc, nr);
    if (a !== b && ((a && a.state === "locked") || (b && b.state === "locked"))) return false;
    return true;
  }
  function pos(e) { return { x: e.c + (e.dir ? e.dir.dx * e.prog : 0), y: e.r + (e.dir ? e.dir.dy * e.prog : 0) }; }
  function nearCell(e) { return e.dir && e.prog >= 0.5 ? { c: e.c + e.dir.dx, r: e.r + e.dir.dy } : { c: e.c, r: e.r }; }

  function advance(e, speed, dt, arrive) {
    var d = speed * dt, guard = 0;
    if (e.dir && e.want && e.want.dx === -e.dir.dx && e.want.dy === -e.dir.dy && e.prog > 0) {
      e.c += e.dir.dx; e.r += e.dir.dy; e.prog = 1 - e.prog; e.dir = e.want;
    }
    if (e.dir && e.prog > 0 && !canStep(e.c, e.r, e.c + e.dir.dx, e.r + e.dir.dy) && !open(e.c + e.dir.dx, e.r + e.dir.dy)) {
      e.prog = 0;                                         // the cell ahead turned back into wall: back off
    }
    while (d > 1e-9 && guard++ < 8) {
      if (e.prog === 0) {
        if (e.want && canStep(e.c, e.r, e.c + e.want.dx, e.r + e.want.dy)) e.dir = e.want;
        else if (e.dir && !canStep(e.c, e.r, e.c + e.dir.dx, e.r + e.dir.dy)) e.dir = null;
        if (!e.dir) break;
      }
      var need = 1 - e.prog;
      if (d < need) { e.prog += d; d = 0; }
      else { d -= need; e.prog = 0; e.c += e.dir.dx; e.r += e.dir.dy; if (arrive) arrive(e); }
    }
  }

  // ---- the player's abilities ---------------------------------------------------------------------------------------
  function doRot() {
    if (state !== "play" || !player) return;
    var at = nearCell(player), f = player.face, tc = at.c + f.dx, tr = at.r + f.dy;
    if (!inside(tc, tr) || grid[tr][tc] !== WALL) return;
    if (rotting.some(function (o) { return o.c === tc && o.r === tr; })) return;
    if (hard[tr][tc]) { log("> rot " + hex(tr * COLS + tc) + ": ACCESS DENIED (firmware)"); if (!manual) Sfx.denied(); return; }
    if (energy < ROT_COST) { log("> rot: not enough infection energy"); if (!manual) Sfx.denied(); return; }
    energy -= ROT_COST;
    rotting.push({ c: tc, r: tr, t: 0 });
    burst((tc + 0.5) * CS + OX, (tr + 0.5) * CS + OY, 30, 2.5, [C.pink, C.pink, C.black, C.white]);
    glitchT = Math.max(glitchT, 0.12);
    if (!manual) Sfx.rot();
    log("> rot " + hex(tr * COLS + tc) + " ... corrupted");
  }
  function doBurst() {
    if (!has("burst") || abil.burst.cd > 0 || state !== "play") return;
    abil.burst.t = 1.6; abil.burst.cd = 6; if (!manual) Sfx.burst(); log("> overclock: speed burst");
  }
  function doTrap() {
    if (!has("trap") || state !== "play") return;
    var at = nearCell(player);
    if (energy < TRAP_COST || traps.length >= 3 || traps.some(function (t) { return t.c === at.c && t.r === at.r; })) { if (!manual) Sfx.denied(); return; }
    energy -= TRAP_COST; traps.push({ c: at.c, r: at.r });
    if (!manual) Sfx.trap(); log("> trap planted at " + hex(at.r * COLS + at.c));
  }
  function doCloak() {
    if (!has("cloak") || abil.cloak.cd > 0 || state !== "play") return;
    abil.cloak.t = 3.5; abil.cloak.cd = 12; if (!manual) Sfx.cloak(); log("> disguise: harmless_data.tmp");
  }
  function doSplit() {
    if (!has("split") || abil.split.cd > 0 || state !== "play") return;
    abil.split.cd = 10;
    var at = nearCell(player);
    decoys.push({ c: at.c, r: at.r, dir: null, want: { dx: -player.face.dx, dy: -player.face.dy }, prog: 0, life: 8, face: DIR.left });
    if (!manual) Sfx.split(); log("> fork(): copy spawned");
    burst(px(at.c), py(at.r), 20, 2, [C.pink, C.white]);
  }
  function cloaked() { return abil.cloak.t > 0; }

  // ---- what happens when something arrives in a cell ---------------------------------------------------------------
  function infectAt(c, r, byCopy, who) {
    var nd = null;
    for (var i = 0; i < nodes.length; i++) if (nodes[i].c === c && nodes[i].r === r) nd = nodes[i];
    if (!nd || nd.inf) return;
    if (nd.honey) {
      nd.honey = false; nd.dead = true;
      burst(px(c), py(r), 50, 3, [C.green, C.white]);
      glitchT = 0.4; if (!manual) Sfx.honey();
      log("> HONEYPOT TRIGGERED at " + hex(r * COLS + c));
      var a = makeAV(c, r, false); a.stun = 0.8; avs.push(a);
      if (byCopy) { who.life = 0; }
      else { player.stun = 1; energy = Math.max(0, energy - 3); alertT = 4; }
      return;
    }
    nd.inf = true;
    var gain = nd.vault ? 3 : nd.restore ? 2 : 1;
    energy = Math.min(MAX_ENERGY, energy + gain);
    score += nd.vault ? 300 : nd.restore ? 500 : 100;
    burst(px(c), py(r), nd.vault || nd.restore ? 46 : 22, 2.4, [C.pink, C.pink, C.white]);
    if (!manual) Sfx.infect();
    dirty = true;
    if (nd.restore) {
      var left = nodes.filter(function (o) { return o.restore && !o.inf; }).length;
      log(left ? "> restore point corrupted: " + left + " left" : "> RESTORE POINTS GONE: System Restore has failed");
      glitchT = 0.3;
    } else if (nd.vault) log("> vault " + hex(r * COLS + c) + " cracked +3 energy");
    checkClear();
  }

  function remaining() { return nodes.filter(function (o) { return !o.inf && !o.honey && !o.dead; }).length; }
  function targetCount() { return nodes.filter(function (o) { return !o.honey && !o.dead; }).length; }

  function checkClear() {
    if (remaining() > 0 || state !== "play") return;
    state = "clear"; stateT = 0;
    var bonus = 1000 + lives * 250;
    score += bonus;
    if (lives < 5) lives++;
    if (!manual) Sfx.clear();
    log("> SECTOR INFECTED. +" + bonus);
    for (var i = 0; i < 6; i++) burst(OX + Math.random() * COLS * CS, OY + Math.random() * ROWS * CS, 30, 3, [C.pink, C.white, C.green]);
    saveBest();
  }

  function saveBest() { if (score > best && !manual && !shot) { best = score; localStorage.setItem("protocolHexBest", String(best)); } }

  function hit() {
    if (god || player.inv > 0 || state !== "play") return;
    lives--;
    glitchT = 0.6;
    var p = pos(player);
    burst(px(p.x), py(p.y), 70, 4, [C.pink, C.white, C.green]);
    if (!manual) Sfx.die();
    log("> process killed by antivirus. " + (lives > 0 ? lives + " left" : "no copies left"));
    if (lives <= 0) { state = "over"; stateT = 0; saveBest(); return; }
    state = "dying"; stateT = 0;
  }

  // ---- one fixed step of the game ------------------------------------------------------------------------------------
  function update(dt) {
    time += dt;
    if (glitchT > 0) glitchT -= dt;
    if (introT > 0) introT -= dt;
    particles = particles.filter(function (p) { p.x += p.vx; p.y += p.vy; p.vx *= 0.94; p.vy *= 0.94; return --p.life > 0; });
    if (state === "dying") { stateT += dt; if (stateT > 1.2) { spawnPlayer(); avs.forEach(function (a) { a.stun = Math.max(a.stun, 1.2); }); state = "play"; } return; }
    if (state !== "play") { stateT += dt; return; }

    ["burst", "cloak"].forEach(function (k) { if (abil[k].t > 0) abil[k].t -= dt; });
    ["burst", "cloak", "split"].forEach(function (k) { if (abil[k].cd > 0) abil[k].cd -= dt; });
    if (player.inv > 0) player.inv -= dt;
    if (energy < 4) energy = Math.min(4, energy + dt * 0.25);   // a slow trickle, so you can never be stuck
    if (alertT > 0) alertT -= dt;

    rotting = rotting.filter(function (o) {                // walls rotting into pathways
      o.t += dt;
      if (o.t >= 0.35) { grid[o.r][o.c] = ROT; dirty = true; return false; }
      return true;
    });

    if (player.stun > 0) player.stun -= dt;
    else advance(player, (6 + Math.min(cycle, 3) * 0.3) * (abil.burst.t > 0 ? 1.75 : 1), dt, function (e) { infectAt(e.c, e.r, false); if (window.PH && PH.onArrive) PH.onArrive(); });

    decoys = decoys.filter(function (d) {
      d.life -= dt;
      advance(d, 5.5, dt, function (e) {
        infectAt(e.c, e.r, true, e);
        var opts = DLIST.filter(function (k) { return canStep(e.c, e.r, e.c + k.dx, e.r + k.dy) && !(e.dir && k.dx === -e.dir.dx && k.dy === -e.dir.dy); });
        if (opts.length) e.want = opts[Math.floor(Math.random() * opts.length)];
        else e.want = e.dir ? { dx: -e.dir.dx, dy: -e.dir.dy } : null;
      });
      if (d.life <= 0) { var p = pos(d); burst(px(p.x), py(p.y), 14, 1.5, [C.pink]); }
      return d.life > 0;
    });

    // the antivirus: a distance field from everything they hunt
    var lures = decoys.map(function (d) { return nearCell(d); });
    if (!cloaked()) lures.push(nearCell(player));
    distField = lures.length ? bfs(lures, canStep) : null;
    var pc = nearCell(player);
    var avSpeed = 3.9 + sector * 0.2 + cycle * 0.4;
    var chaseR = 7 + sector;
    avs.forEach(function (a) {
      if (a.stun > 0) { a.stun -= dt; return; }
      var hunting = distField && (alertT > 0 || (distField[a.r][a.c] >= 0 && distField[a.r][a.c] <= chaseR));
      advance(a, avSpeed * (alertT > 0 ? 1.25 : 1) * (hunting ? 1 : 0.85), dt, function (e) {
        for (var i = 0; i < traps.length; i++) if (traps[i].c === e.c && traps[i].r === e.r) {
          e.stun = 5; traps.splice(i, 1); burst(px(e.c), py(e.r), 30, 2, [C.pink, C.green]); if (!manual) Sfx.snare(); log("> antivirus snared in corruption"); return;
        }
        chooseAV(e, hunting);
      });
      if (a.prog === 0 && !a.dir) chooseAV(a, hunting);
      if (a.dir) a.face = a.dir;
    });

    // catching: the player, or a copy
    var pp = pos(player);
    avs.forEach(function (a) {
      if (a.stun > 0) return;
      var ap = pos(a);
      decoys.forEach(function (d) { var dp = pos(d); if (Math.abs(dp.x - ap.x) + Math.abs(dp.y - ap.y) < 0.7) d.life = 0; });
      if (!cloaked() && Math.abs(pp.x - ap.x) + Math.abs(pp.y - ap.y) < 0.7) hit();
    });
    if (state !== "play") return;

    systems(dt, pp);
  }

  function chooseAV(e, hunting) {
    var back = e.dir ? { dx: -e.dir.dx, dy: -e.dir.dy } : null;
    var opts = DLIST.filter(function (k) { return canStep(e.c, e.r, e.c + k.dx, e.r + k.dy); });
    if (!opts.length) { e.want = null; return; }
    if (hunting && distField) {
      var bestD = 1e9, pick = null;
      opts.forEach(function (k) { var v = distField[e.r + k.dy][e.c + k.dx]; if (v >= 0 && v < bestD) { bestD = v; pick = k; } });
      if (pick && Math.random() > 0.08) { e.want = pick; return; }
    }
    var fwd = opts.filter(function (k) { return !back || k.dx !== back.dx || k.dy !== back.dy; });
    var list = fwd.length ? fwd : opts;
    e.want = list[Math.floor(Math.random() * list.length)];
  }

  function rotCell(c, r) { return inside(c, r) && grid[r][c] === ROT; }

  function systems(dt, pp) {
    var pc = nearCell(player), safe = cloaked() || player.inv > 0 || god;
    if (firewall) {                                      // a wall of fire sweeping across the board
      var f = firewall;
      f.t -= dt;
      if (f.phase === "wait" && f.t <= 0) { f.phase = "warn"; f.t = 1.6; log("> firewall sweep incoming " + (f.dir > 0 ? ">>>" : "<<<")); if (!manual) Sfx.alarm(); }
      else if (f.phase === "warn" && f.t <= 0) { f.phase = "sweep"; f.x = f.dir > 0 ? -0.5 : COLS - 0.5; if (!manual) Sfx.sweep(); }
      else if (f.phase === "sweep") {
        f.x += f.dir * (4.2 + cycle * 0.5) * dt;
        traps = traps.filter(function (t) { return Math.abs(t.c - f.x) > 0.5; });
        decoys.forEach(function (d) { if (Math.abs(pos(d).x - f.x) < 0.5) d.life = 0; });
        if (!safe && Math.abs(pp.x - f.x) < 0.5 && !rotCell(pc.c, pc.r)) hit();
        if (f.x > COLS + 0.5 || f.x < -1) { f.phase = "wait"; f.t = 5.5; f.dir = -f.dir; }
      }
    }
    zones.forEach(function (z) {                          // quarantine zones: open, warning, locked
      z.t -= dt;
      if (z.state === "open" && z.t <= 0) { z.state = "warn"; z.t = 1.8; }
      else if (z.state === "warn" && z.t <= 0) {
        z.state = "locked"; z.t = 5;
        if (zoneOf(pc.c, pc.r) === z && !god) {
          log("> QUARANTINE: process isolated. guard deployed");
          if (!manual) Sfx.alarm();
          var cells = [];
          for (var r = z.r0; r <= z.r1; r++) for (var c = z.c0; c <= z.c1; c++) if (open(c, r) && Math.abs(c - pc.c) + Math.abs(r - pc.r) >= 4) cells.push({ c: c, r: r });
          if (cells.length) { var s = cells[Math.floor(Math.random() * cells.length)]; z.guard = makeAV(s.c, s.r, true); z.guard.stun = 0.6; avs.push(z.guard); }
        }
      } else if (z.state === "locked") {
        if (zoneOf(pc.c, pc.r) === z) energy = Math.max(0, energy - dt * 0.6);
        if (z.t <= 0) {
          z.state = "open"; z.t = 7;
          if (z.guard) { var g = z.guard, gp = pos(g); avs.splice(avs.indexOf(g), 1); burst(px(gp.x), py(gp.y), 20, 2, [C.green]); z.guard = null; }
        }
      }
    });
    if (scanner) {                                         // a radar beam sweeping round from the middle
      scanner.a = (scanner.a + dt * 1.05) % (Math.PI * 2);
      if (scanner.cool > 0) scanner.cool -= dt;
      var ang = Math.atan2(pp.y - 7, pp.x - 15), diff = Math.abs(((ang - scanner.a) % (Math.PI * 2) + Math.PI * 3) % (Math.PI * 2) - Math.PI);
      if (!safe && diff < 0.09 && scanner.cool <= 0 && !rotCell(pc.c, pc.r)) {
        alertT = 4; scanner.cool = 2; glitchT = 0.2;
        log("> SCANNER: INTRUSION DETECTED at " + hex(pc.r * COLS + pc.c)); if (!manual) Sfx.alarm();
      }
    }
    if (restore) {                                         // System Restore: a wave that undoes the corruption
      var R = restore, pts = nodes.filter(function (o) { return o.restore; }), left = pts.filter(function (o) { return !o.inf; }).length;
      if (left === 0) { R.phase = "broken"; return; }
      var reach = 6 + left * 5;
      R.t -= dt;
      if (R.phase === "wait" && R.t <= 0) { R.phase = "warn"; R.t = 2.2; log("> System Restore: rolling back to restore point..."); if (!manual) Sfx.restore(); }
      else if (R.phase === "warn" && R.t <= 0) { R.phase = "wave"; R.R = 0; R.prevR = 0; }
      else if (R.phase === "wave") {
        R.prevR = R.R; R.R += dt * 11;
        var lost = 0;
        nodes.forEach(function (o) {
          if (!o.inf || o.restore) return;
          var d = Math.hypot(o.c - R.core.c, o.r - R.core.r);
          if (d >= R.prevR && d < R.R && d <= reach) { o.inf = false; lost++; burst(px(o.c), py(o.r), 14, 1.8, [C.green, C.white]); }
        });
        for (var r = 0; r < ROWS; r++) for (var c = 0; c < COLS; c++) {
          if (grid[r][c] !== ROT) continue;
          var d = Math.hypot(c - R.core.c, r - R.core.r);
          if (d >= R.prevR && d < R.R && d <= reach && !occupied(c, r)) { grid[r][c] = WALL; dirty = true; }
        }
        if (lost) { dirty = true; log("> restored " + lost + " node" + (lost > 1 ? "s" : "")); }
        if (R.R > reach) { R.phase = "wait"; R.t = 13; }
      }
    }
  }

  function occupied(c, r) {
    var all = [player].concat(avs, decoys);
    return all.some(function (e) { var n = nearCell(e); return (e.c === c && e.r === r) || (n.c === c && n.r === r); });
  }

  // ---- drawing ------------------------------------------------------------------------------------------------------
  function px(c) { return OX + (c + 0.5) * CS; }
  function py(r) { return OY + (r + 0.5) * CS; }
  function glow(col, b) { ctx.shadowColor = col; ctx.shadowBlur = b; ctx.fillStyle = col; ctx.strokeStyle = col; }
  function noGlow() { ctx.shadowBlur = 0; }

  function corrupted() {                                   // which cells the pink is bleeding into
    var m = [];
    for (var r = 0; r < ROWS; r++) { m.push([]); for (var c = 0; c < COLS; c++) m[r].push(grid[r][c] === ROT ? 2 : 0); }
    nodes.forEach(function (o) {
      if (!o.inf) return;
      for (var r = o.r - 2; r <= o.r + 2; r++) for (var c = o.c - 2; c <= o.c + 2; c++)
        if (inside(c, r) && Math.abs(c - o.c) + Math.abs(r - o.r) <= 2) m[r][c] = Math.max(m[r][c], 1);
    });
    return m;
  }

  function buildBoard() {
    var b = board.getContext("2d"), m = corrupted(), r, c;
    b.fillStyle = C.black; b.fillRect(0, 0, W, H);
    for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) {
      var x = OX + c * CS, y = OY + r * CS;
      if (grid[r][c] === WALL) {
        b.fillStyle = hard[r][c] ? "#00253d" : C.navy;
        b.fillRect(x + 1, y + 1, CS - 2, CS - 2);
        b.fillStyle = hard[r][c] ? C.deep : "#002a44";
        if (hard[r][c]) { for (var k = 0; k < 3; k++) b.fillRect(x + 4 + k * 8, y + 4, 2, CS - 8); }
        else { b.fillRect(x + 6, y + 6, 3, 3); b.fillRect(x + CS - 9, y + CS - 9, 3, 3); }
      } else if (grid[r][c] === ROT) {
        b.fillStyle = "#1f0820"; b.fillRect(x, y, CS, CS);
        b.fillStyle = C.pink;
        var seed = rng(r * 131 + c * 7);
        for (k = 0; k < 7; k++) { b.globalAlpha = 0.25 + seed() * 0.4; b.fillRect(x + seed() * (CS - 6), y + seed() * (CS - 3), 2 + seed() * 8, 2); }
        b.globalAlpha = 1;
      }
    }
    // the traces: green copper, pink where the infection has bled into them
    b.lineCap = "round";
    for (r = 0; r < ROWS; r++) for (c = 0; c < COLS; c++) {
      if (grid[r][c] === WALL) continue;
      [[1, 0], [0, 1]].forEach(function (d) {
        var nc = c + d[0], nr = r + d[1];
        if (!open(nc, nr)) return;
        var pink = m[r][c] || m[nr][nc];
        b.strokeStyle = pink ? C.pink : C.green;
        b.shadowColor = b.strokeStyle; b.shadowBlur = 6;
        b.globalAlpha = pink ? 0.75 : 0.45; b.lineWidth = 3;
        b.beginPath(); b.moveTo(px(c), py(r)); b.lineTo(px(nc), py(nr)); b.stroke();
        b.globalAlpha = 0.2; b.lineWidth = 1;
        b.beginPath(); b.moveTo(px(c) + d[1] * 6, py(r) + d[0] * 6); b.lineTo(px(nc) + d[1] * 6, py(nr) + d[0] * 6); b.stroke();
      });
      var n = DLIST.filter(function (d) { return open(c + d.dx, r + d.dy); }).length;
      if (n >= 3) {
        b.globalAlpha = 0.7; b.fillStyle = m[r][c] ? C.pink : C.green;
        b.beginPath(); b.arc(px(c), py(r), 3, 0, Math.PI * 2); b.fill();
      }
    }
    b.globalAlpha = 1; b.shadowBlur = 0;
    if (restore) {                                         // the restore core, printed on the board
      b.strokeStyle = C.green; b.globalAlpha = 0.35; b.lineWidth = 2;
      b.beginPath(); b.arc(px(15), py(7), 13, 0, Math.PI * 2); b.stroke();
      b.beginPath(); b.arc(px(15), py(7), 6, 0, Math.PI * 2); b.stroke();
      b.globalAlpha = 1;
    }
    dirty = false;
  }

  function drawHex(x, y, r, rot) {
    ctx.beginPath();
    for (var i = 0; i < 6; i++) { var a = rot + i * Math.PI / 3; ctx[i ? "lineTo" : "moveTo"](x + Math.cos(a) * r, y + Math.sin(a) * r); }
    ctx.closePath();
  }

  function drawNode(o) {
    var x = px(o.c), y = py(o.r), s = o.vault || o.restore ? 8 : 6;
    if (o.dead) return;
    if (o.inf) {
      ctx.globalAlpha = 0.1 + 0.05 * Math.sin(time * 3 + o.flick);
      ctx.fillStyle = C.pink; ctx.beginPath(); ctx.arc(x, y, CS * 1.3, 0, Math.PI * 2); ctx.fill();
      ctx.globalAlpha = 1;
      glow(C.pink, 16 + 6 * Math.sin(time * 4 + o.flick));
      ctx.fillRect(x - s, y - s, s * 2, s * 2);
      noGlow(); ctx.fillStyle = C.black; ctx.fillRect(x - s + 3, y - s + 3, s * 2 - 6, s * 2 - 6);
      ctx.fillStyle = C.pink; ctx.fillRect(x - 2, y - 2, 4, 4);
    } else {
      var col = o.restore ? C.sky : C.green;
      glow(col, 10);
      ctx.lineWidth = 2; ctx.strokeRect(x - s, y - s, s * 2, s * 2);
      for (var k = -1; k <= 1; k += 2) { ctx.fillRect(x - s - 4, y + k * 3 - 1, 3, 2); ctx.fillRect(x + s + 1, y + k * 3 - 1, 3, 2); }
      ctx.fillRect(x - 2, y - 2, 4, 4);
      noGlow();
      if (o.restore) { ctx.strokeStyle = C.sky; ctx.lineWidth = 1; ctx.beginPath(); ctx.arc(x, y, 13 + 2 * Math.sin(time * 3), 0, Math.PI * 2); ctx.stroke(); }
      if (o.honey && ((time + o.flick) % 3.2) < 0.18) {
        ctx.fillStyle = C.green; ctx.font = "bold 14px Consolas, monospace"; ctx.textAlign = "center"; ctx.fillText("?", x, y - 11);
      }
    }
  }

  function drawAV(a) {
    var p = pos(a), x = px(p.x), y = py(p.y);
    var blinkOn = a.stun > 0 && Math.floor(time * 10) % 2;
    ctx.globalAlpha = a.stun > 0 ? 0.5 : 1;
    glow(C.green, alertT > 0 ? 24 : 14);
    ctx.beginPath();                                      // a shield
    ctx.moveTo(x, y - 11); ctx.lineTo(x + 10, y - 7); ctx.lineTo(x + 8, y + 4); ctx.lineTo(x, y + 11); ctx.lineTo(x - 8, y + 4); ctx.lineTo(x - 10, y - 7); ctx.closePath();
    ctx.fill(); noGlow();
    ctx.fillStyle = C.black;
    ctx.beginPath(); ctx.ellipse(x, y - 1, 6, 3.5, 0, 0, Math.PI * 2); ctx.fill();
    ctx.fillStyle = blinkOn ? C.black : (alertT > 0 ? C.white : C.green);
    ctx.beginPath(); ctx.arc(x + a.face.dx * 2.5, y - 1 + a.face.dy * 1.5, 2.2, 0, Math.PI * 2); ctx.fill();
    if (a.guard) { ctx.strokeStyle = C.green; ctx.lineWidth = 1; ctx.strokeRect(x - 13, y - 13, 26, 26); }
    if (a.stun > 0) {
      ctx.strokeStyle = C.pink; ctx.lineWidth = 1.5;
      for (var k = 0; k < 3; k++) { ctx.beginPath(); ctx.arc(x, y, 9 + k * 3, time * 3 + k, time * 3 + k + 2); ctx.stroke(); }
    }
    ctx.globalAlpha = 1;
  }

  function drawPlayer(e, copy) {
    var p = pos(e), x = px(p.x), y = py(p.y);
    if (!copy && cloaked()) {                             // disguised as a harmless packet of data
      ctx.globalAlpha = 0.55 + 0.2 * Math.sin(time * 20);
      ctx.strokeStyle = C.green; ctx.lineWidth = 1; ctx.strokeRect(x - 9, y - 7, 18, 14);
      ctx.fillStyle = C.green; ctx.font = "9px Consolas, monospace"; ctx.textAlign = "center"; ctx.fillText("01", x, y + 3);
      ctx.globalAlpha = 1; return;
    }
    if (!copy && e.inv > 0 && Math.floor(time * 12) % 2) return;
    ctx.globalAlpha = copy ? 0.55 : 1;
    glow(C.pink, copy ? 10 : 22);
    drawHex(x, y, 10, time * (copy ? -2 : 2)); ctx.fill();
    noGlow();
    ctx.fillStyle = C.black; drawHex(x, y, 5, -time * 3); ctx.fill();
    ctx.fillStyle = C.pink;
    for (var i = 0; i < 6; i++) { var a = time * 2 + i * Math.PI / 3; ctx.fillRect(x + Math.cos(a) * 13 - 1, y + Math.sin(a) * 13 - 1, 2.5, 2.5); }
    if (!copy && abil.burst.t > 0) { ctx.globalAlpha = 0.35; ctx.fillStyle = C.pink; ctx.fillRect(x - e.face.dx * 18 - 4, y - e.face.dy * 18 - 4, 8, 8); }
    ctx.globalAlpha = 1;
  }

  function drawSystems() {
    zones.forEach(function (z) {
      var x = OX + z.c0 * CS, y = OY + z.r0 * CS, w = (z.c1 - z.c0 + 1) * CS, h = (z.r1 - z.r0 + 1) * CS;
      if (z.state === "locked") {
        ctx.fillStyle = C.green; ctx.globalAlpha = 0.08; ctx.fillRect(x, y, w, h); ctx.globalAlpha = 1;
        glow(C.green, 14); ctx.lineWidth = 3; ctx.strokeRect(x, y, w, h); noGlow();
        ctx.fillStyle = C.green; ctx.font = "11px Consolas, monospace"; ctx.textAlign = "left";
        ctx.fillText("QUARANTINE " + Math.ceil(z.t) + "s", x + 4, y - 4);
      } else {
        var flash = z.state === "warn" && Math.floor(time * 8) % 2;
        ctx.strokeStyle = flash ? C.green : C.deep; ctx.lineWidth = 1; ctx.setLineDash([6, 6]);
        ctx.strokeRect(x, y, w, h); ctx.setLineDash([]);
        if (z.state === "warn") { ctx.fillStyle = C.green; ctx.font = "11px Consolas, monospace"; ctx.textAlign = "left"; ctx.fillText("LOCKDOWN " + z.t.toFixed(1), x + 4, y - 4); }
      }
    });
    if (scanner) {
      var cx = px(15), cy = py(7), R = 600;
      ctx.fillStyle = C.green; ctx.globalAlpha = 0.09;
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.arc(cx, cy, R, scanner.a - 0.5, scanner.a + 0.09); ctx.closePath(); ctx.fill();
      ctx.globalAlpha = 0.5; ctx.strokeStyle = C.green; ctx.lineWidth = 2;
      ctx.beginPath(); ctx.moveTo(cx, cy); ctx.lineTo(cx + Math.cos(scanner.a) * R, cy + Math.sin(scanner.a) * R); ctx.stroke();
      ctx.globalAlpha = 1;
      glow(C.green, 14); ctx.beginPath(); ctx.arc(cx, cy, 6, 0, Math.PI * 2); ctx.fill(); noGlow();
    }
    if (firewall && firewall.phase !== "wait") {
      var f = firewall;
      if (f.phase === "warn") {
        if (Math.floor(time * 6) % 2) {
          ctx.fillStyle = C.green; ctx.font = "bold 18px Consolas, monospace"; ctx.textAlign = "center";
          ctx.fillText(f.dir > 0 ? "FIREWALL >>>" : "<<< FIREWALL", f.dir > 0 ? OX + 80 : OX + COLS * CS - 80, OY + ROWS * CS / 2);
        }
      } else {
        var fx = OX + (f.x + 0.5) * CS;
        glow(C.green, 30);
        ctx.globalAlpha = 0.85; ctx.fillRect(fx - 4, OY, 8, ROWS * CS);
        ctx.globalAlpha = 0.5;
        for (var yy = OY; yy < OY + ROWS * CS; yy += 12) {
          var fl = 6 + 8 * Math.abs(Math.sin(yy * 0.3 + time * 20));
          ctx.beginPath(); ctx.moveTo(fx, yy); ctx.lineTo(fx - f.dir * fl, yy + 6); ctx.lineTo(fx, yy + 12); ctx.fill();
        }
        ctx.globalAlpha = 1; noGlow();
      }
    }
    if (restore && restore.phase !== "broken") {
      var cx2 = px(15), cy2 = py(7);
      var pulse = restore.phase === "warn" ? 0.5 + 0.5 * Math.sin(time * 14) : 0.4;
      glow(C.green, 16); ctx.lineWidth = 2; ctx.globalAlpha = pulse;
      ctx.beginPath(); ctx.arc(cx2, cy2, 13, time * 2, time * 2 + 4.5); ctx.stroke(); ctx.globalAlpha = 1;
      if (restore.phase === "wave") {
        ctx.lineWidth = 6; ctx.globalAlpha = 0.6;
        ctx.beginPath(); ctx.arc(cx2, cy2, restore.R * CS, 0, Math.PI * 2); ctx.stroke(); ctx.globalAlpha = 1;
      }
      noGlow();
    }
    traps.forEach(function (t) {
      var x = px(t.c), y = py(t.r);
      ctx.strokeStyle = C.pink; ctx.lineWidth = 1.5; ctx.globalAlpha = 0.8;
      for (var k = 0; k < 4; k++) { var a = k * Math.PI / 4 + time; ctx.beginPath(); ctx.moveTo(x - Math.cos(a) * 11, y - Math.sin(a) * 11); ctx.lineTo(x + Math.cos(a) * 11, y + Math.sin(a) * 11); ctx.stroke(); }
      ctx.beginPath(); ctx.arc(x, y, 6, 0, Math.PI * 2); ctx.stroke(); ctx.globalAlpha = 1;
    });
    rotting.forEach(function (o) {
      var x = OX + o.c * CS, y = OY + o.r * CS, k = o.t / 0.35;
      ctx.fillStyle = C.pink; ctx.globalAlpha = 0.4 + 0.4 * Math.random();
      for (var i = 0; i < 6; i++) ctx.fillRect(x + Math.random() * CS * k, y + Math.random() * CS, 4, 2 + Math.random() * 6);
      ctx.globalAlpha = 1;
    });
  }

  function drawHUD() {
    var S = SECTORS[sector];
    ctx.textAlign = "left"; ctx.font = "bold 16px Consolas, monospace";
    glow(C.green, 8); ctx.fillText("PROTOCOL", 16, 22); noGlow();
    ctx.fillStyle = C.pink; ctx.fillText(".hex", 16 + ctx.measureText("PROTOCOL").width, 22);
    ctx.fillStyle = C.sky; ctx.font = "13px Consolas, monospace";
    ctx.fillText("SECTOR " + (sector + 1) + (cycle ? "." + (cycle + 1) : "") + " " + S.name, 16, 40);
    ctx.fillStyle = C.green;
    ctx.fillText("SCORE " + score, 250, 22); ctx.fillStyle = C.sky; ctx.fillText("BEST " + Math.max(best, score), 250, 40);
    ctx.fillStyle = C.pink;
    ctx.fillText("NODES " + (targetCount() - remaining()) + "/" + targetCount(), 390, 22);
    for (var i = 0; i < lives; i++) { drawHex(400 + i * 16, 36, 6, 0); ctx.fill(); }
    // the energy bar
    ctx.fillStyle = C.sky; ctx.fillText("ENERGY", 520, 22);
    for (i = 0; i < MAX_ENERGY; i++) {
      ctx.fillStyle = i < Math.floor(energy) ? C.pink : C.navy;
      ctx.fillRect(578 + i * 9, 12, 7, 12);
    }
    // the abilities
    var x = 520;
    ABIL.forEach(function (a) {
      var have = has(a.id), cd = a.id === "rot" ? (energy < ROT_COST ? 1 : 0) : a.id === "trap" ? (energy < TRAP_COST ? 1 : 0) : abil[a.id].cd;
      ctx.font = "10px Consolas, monospace";
      ctx.fillStyle = !have ? "#24384a" : cd > 0 ? C.deep : (abil[a.id] && abil[a.id].t > 0 ? C.white : C.green);
      ctx.fillText(have ? a.key + ":" + a.name : "LOCKED", x, 40);
      x += a.id === "rot" ? 76 : a.id === "burst" ? 84 : 62;
    });
    if (Sfx.isMuted()) { ctx.fillStyle = C.sky; ctx.font = "10px Consolas, monospace"; ctx.fillText("MUTED", 905, 22); }
    if (alertT > 0) { ctx.fillStyle = C.green; ctx.font = "bold 12px Consolas, monospace"; if (Math.floor(time * 6) % 2) ctx.fillText("!! DETECTED", 860, 40); }
    // the terminal log
    ctx.font = "12px Consolas, monospace";
    for (i = 0; i < logLines.length; i++) {
      var L = logLines[i];
      ctx.globalAlpha = 0.45 + 0.55 * (i + 1) / logLines.length;
      ctx.fillStyle = L.s.indexOf("!") >= 0 || /[A-Z]{5}/.test(L.s.slice(2)) ? C.pink : C.green;
      var shown = time - L.t < 0.6 ? L.s.slice(0, Math.floor((time - L.t) / 0.6 * L.s.length)) : L.s;
      ctx.fillText(shown, 16 + Math.floor(i / 2) * 470, H - 26 + (i % 2) * 15);
    }
    ctx.globalAlpha = 1;
  }

  function centre(text, y, size, col, blur) {
    ctx.textAlign = "center"; ctx.font = "bold " + size + "px Consolas, monospace";
    glow(col, blur || 0); ctx.fillText(text, W / 2, y); noGlow();
  }

  function draw() {
    if (!grid) { ctx.fillStyle = C.black; ctx.fillRect(0, 0, W, H); return; }
    if (dirty) buildBoard();
    ctx.drawImage(board, 0, 0);
    drawSystems();
    nodes.forEach(drawNode);
    decoys.forEach(function (d) { drawPlayer(d, true); });
    avs.forEach(drawAV);
    if (state !== "dying" && state !== "over") drawPlayer(player, false);
    particles.forEach(function (p) {
      ctx.globalAlpha = p.life / p.max; glow(p.c, 8); ctx.fillRect(p.x - 1.5, p.y - 1.5, 3, 3);
    });
    ctx.globalAlpha = 1; noGlow();
    // scanlines, like an old terminal
    ctx.fillStyle = C.black; ctx.globalAlpha = 0.18;
    for (var y = 0; y < H; y += 3) ctx.fillRect(0, y, W, 1);
    ctx.globalAlpha = 1;
    drawHUD();

    var S = SECTORS[sector];
    if (introT > 0 && state === "play") {
      ctx.globalAlpha = Math.min(1, introT / 0.6);
      ctx.fillStyle = C.black; ctx.fillRect(0, H / 2 - 64, W, 118);
      centre("SECTOR " + (sector + 1) + ": " + S.name, H / 2 - 24, 30, C.green, 16);
      ctx.fillStyle = C.white; ctx.font = "15px Consolas, monospace"; ctx.fillText(S.intro, W / 2, H / 2 + 6);
      ctx.fillStyle = C.pink; ctx.fillText(S.tip, W / 2, H / 2 + 30);
      ctx.globalAlpha = 1;
    }
    var dim = function () { ctx.fillStyle = C.black; ctx.globalAlpha = 0.7; ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1; };
    if (paused) { dim(); centre("PAUSED", H / 2 - 10, 54, C.green, 22); ctx.fillStyle = C.white; ctx.font = "16px Consolas, monospace"; ctx.fillText("P to resume", W / 2, H / 2 + 26); }
    else if (state === "clear") {
      dim(); centre("SECTOR INFECTED", H / 2 - 20, 46, C.pink, 24);
      ctx.fillStyle = C.white; ctx.font = "16px Consolas, monospace";
      ctx.fillText(stateT > 0.8 ? "Any key or tap: next sector" : "", W / 2, H / 2 + 20);
    } else if (state === "over") {
      dim(); centre("PROCESS TERMINATED", H / 2 - 30, 44, C.green, 24);
      ctx.fillStyle = C.white; ctx.font = "17px Consolas, monospace";
      ctx.fillText("Score " + score + "   Best " + best, W / 2, H / 2 + 8);
      ctx.fillStyle = C.pink; ctx.fillText(stateT > 1 ? "Any key or tap to reinfect" : "", W / 2, H / 2 + 40);
    } else if (state === "won") {
      dim(); centre("SYSTEM COMPROMISED", H / 2 - 40, 46, C.pink, 26);
      ctx.fillStyle = C.white; ctx.font = "17px Consolas, monospace";
      ctx.fillText("Every sector is yours. Score " + score, W / 2, H / 2);
      ctx.fillStyle = C.green; ctx.fillText(stateT > 1 ? "Any key: the system reboots, harder" : "", W / 2, H / 2 + 34);
    }
    if (glitchT > 0) {                                    // the curse: the picture tears
      for (var i = 0; i < 6; i++) {
        var gy = Math.random() * H, gh = 6 + Math.random() * 30, gx = (Math.random() - 0.5) * 40;
        ctx.drawImage(canvas, 0, gy, W, gh, gx, gy, W, gh);
      }
      ctx.fillStyle = C.pink; ctx.globalAlpha = 0.08; ctx.fillRect(0, 0, W, H); ctx.globalAlpha = 1;
    }
  }

  function burst(x, y, n, speed, cols) {
    if (particles.length > 800) return;
    for (var i = 0; i < n; i++) {
      var a = Math.random() * Math.PI * 2, v = speed * (0.2 + Math.random() * 0.8), life = 25 + Math.random() * 35;
      particles.push({ x: x, y: y, vx: Math.cos(a) * v, vy: Math.sin(a) * v, life: life, max: life, c: cols[Math.floor(Math.random() * cols.length)] });
    }
  }

  // ---- flow and input ------------------------------------------------------------------------------------------------
  function newGame() { score = 0; lives = 3; energy = 4; cycle = 0; logLines = []; loadSector(0); }
  function next() {
    if (state === "clear" && stateT > 0.8) {
      if (sector + 1 < SECTORS.length) loadSector(sector + 1);
      else { state = "won"; stateT = 0; saveBest(); }
      return true;
    }
    if (state === "won" && stateT > 1) { cycle++; loadSector(0); return true; }
    if (state === "over" && stateT > 1) { newGame(); return true; }
    return false;
  }
  function startGame() {
    if (started) return;
    started = true;
    splash.classList.add("gone");
    document.body.classList.add("playing");
    newGame();
  }
  function steer(d) { if (player && state === "play") { player.want = d; player.face = d; } }

  var KEYDIR = { arrowup: DIR.up, w: DIR.up, arrowdown: DIR.down, s: DIR.down, arrowleft: DIR.left, a: DIR.left, arrowright: DIR.right, d: DIR.right };
  document.addEventListener("keydown", function (e) {
    var k = e.key.toLowerCase();
    if (!started) { startGame(); e.preventDefault(); return; }
    if (next()) { e.preventDefault(); return; }
    if (k === "p") { if (state === "play") paused = !paused; }
    else if (k === "m") Sfx.toggle();
    else if (paused) return;
    else if (KEYDIR[k]) steer(KEYDIR[k]);
    else if (k === " ") doRot();
    else if (k === "shift") doBurst();
    else if (k === "c") doTrap();
    else if (k === "q") doCloak();
    else if (k === "f") doSplit();
    else return;
    e.preventDefault();
  });
  splash.addEventListener("click", startGame);

  if ("ontouchstart" in window || navigator.maxTouchPoints > 0) document.body.classList.add("touch");
  var touch = null;
  canvas.addEventListener("touchstart", function (e) {
    if (!started) { startGame(); return; }
    if (next()) return;
    touch = { x: e.touches[0].clientX, y: e.touches[0].clientY, t: Date.now() };
  }, { passive: true });
  canvas.addEventListener("touchmove", function (e) {
    if (!touch) return;
    var dx = e.touches[0].clientX - touch.x, dy = e.touches[0].clientY - touch.y;
    if (Math.max(Math.abs(dx), Math.abs(dy)) > 22) {
      steer(Math.abs(dx) > Math.abs(dy) ? (dx > 0 ? DIR.right : DIR.left) : (dy > 0 ? DIR.down : DIR.up));
      touch = { x: e.touches[0].clientX, y: e.touches[0].clientY, t: Date.now() };
    }
  }, { passive: true });
  canvas.addEventListener("touchend", function () {
    if (touch && Date.now() - touch.t < 200) doRot();      // a quick tap rots the wall ahead
    touch = null;
  }, { passive: true });
  canvas.addEventListener("mousedown", function () { if (!started) startGame(); else next(); });
  Array.prototype.forEach.call(document.querySelectorAll("#touch button"), function (b) {
    b.addEventListener("pointerdown", function (e) {
      e.preventDefault(); b.classList.add("down");
      if (next()) return;
      var k = b.getAttribute("data-k");
      if (k === "pause") { if (state === "play") paused = !paused; }
      else if (paused) return;
      else ({ rot: doRot, burst: doBurst, trap: doTrap, cloak: doCloak, split: doSplit })[k]();
    });
    ["pointerup", "pointercancel", "pointerleave"].forEach(function (ev) { b.addEventListener(ev, function () { b.classList.remove("down"); }); });
  });

  var acc = 0, last = 0;
  function loop(now) {
    if (!last) last = now;
    acc += Math.min(100, now - last) / 1000; last = now;
    while (acc >= STEP) { acc -= STEP; if (!paused && !manual && started) update(STEP); }
    draw();
    requestAnimationFrame(loop);
  }

  // for the tests and the screenshots: drive the game by hand
  window.PH = {
    sectors: SECTORS.length,
    load: function (i, c) { manual = true; if (!started) startGame(); cycle = c || 0; loadSector(i); introT = 0; },
    god: function (v) { god = v; },
    steer: function (d) { steer(DIR[d]); },
    halt: function () { player.dir = null; player.want = null; },
    rot: doRot, burst: doBurst, trap: doTrap, cloak: doCloak, split: doSplit,
    run: function (n) { for (var i = 0; i < n; i++) update(STEP); },
    info: function () {
      return { state: state, sector: sector, c: player.c, r: player.r, prog: player.prog, dir: player.dir, energy: energy, score: score,
               lives: lives, remaining: remaining(), target: targetCount(), grid: grid, hard: hard, zones: zones.map(function (z) { return z.state; }),
               nodes: nodes.map(function (o) { return { c: o.c, r: o.r, inf: o.inf, honey: o.honey, dead: !!o.dead, restore: o.restore, vault: o.vault }; }) };
    },
    draw: function () { draw(); },
    set: function (o) { if ("energy" in o) energy = o.energy; if ("alert" in o) alertT = o.alert; if ("glitch" in o) glitchT = o.glitch; if ("intro" in o) introT = o.intro; if ("score" in o) score = o.score; },
    live: function () { manual = false; }
  };

  if (shot) {                                             // a good-looking moment for the README
    splash.style.display = "none";
    PH.load(3);
    var r0 = rng(5);
    nodes.forEach(function (o) { if (r0() < 0.55 && !o.vault) o.inf = true; });
    for (var k = 0; k < 9; k++) { var rc = 2 + Math.floor(r0() * 27), rr = 1 + Math.floor(r0() * 13); if (grid[rr][rc] === WALL && !hard[rr][rc]) grid[rr][rc] = ROT; }
    player.c = 13; player.r = 7; player.dir = null; player.inv = 0; dirty = true;
    while (!open(player.c, player.r)) player.c++;
    scanner.a = 2.3; alertT = 0; energy = 9; score = 4200;
    traps.push({ c: player.c + 2, r: player.r });
    for (var s = 0; s < 40; s++) update(STEP);
    log("> rot 0x00F3 ... corrupted"); log("> SCANNER: INTRUSION DETECTED at 0x00E2");
    time = 3;
    manual = true;
  }
  requestAnimationFrame(loop);
})();
