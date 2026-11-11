// Acustra promo: three pieces in one Full HD film, after Aculyre's promo.mjs.
// The guitars' own strings are the picture: each player of the quartet has a
// band drawn as the Rack panel's fretboard display draws it, its six strings
// with the bridge on the left, frets 1-20 at their true spacing and the nut on
// the right, low E at the bottom. A stopped string sounds from its fret to the
// bridge: that length is lit and a fingertip sits behind the fret. The
// performed notes ride in along their string from the right and are plucked
// at the pluck line. Frames are a deterministic function of time and the
// timeline (notes from the performance; each string's level, state and fret
// from the engine's read-only 60 Hz observers, AcustraEngine::getStringActivity).
// Brightness follows the observed level (dB re 1 N, the smoothed string
// reaction force); the strings' visible sway is illustrative: its size
// follows that level, its rate is not the pitch. Colours are the panel's
// legend: gold strings are held, teal ones released and still ringing, ivory
// ones nobody played, ringing only in sympathy through the bridge.
// usage: node promo.mjs <timeline.json> <audio.wav> <out.mp4> [--portrait] [--still t1 t2 ...]
// --portrait draws a 1080x1920 short, every word inside the project's TikTok
// safe box (x 130-821, y 307-1190); the strings and progress bar are artwork
// and run below and behind it.
import fs from 'node:fs';
import path from 'node:path';
import {spawn} from 'node:child_process';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const {createCanvas, loadImage, GlobalFonts} = require('@napi-rs/canvas');
const A = path.resolve('../assets');
GlobalFonts.registerFromPath(`${A}/LibreBaskerville-wght.ttf`, 'Bask');
GlobalFonts.registerFromPath('/usr/share/fonts/opentype/inter/Inter-Regular.otf', 'InterR');
GlobalFonts.registerFromPath('/usr/share/fonts/opentype/inter/Inter-Medium.otf', 'InterM');
GlobalFonts.registerFromPath('/usr/share/fonts/opentype/inter/Inter-SemiBold.otf', 'InterS');
const logo = await loadImage(`${A}/logo.png`), wordmark = await loadImage(`${A}/wordmark.png`);

let [timelinePath, audioPath, outPath, ...rest] = process.argv.slice(2);
const P = rest[0] === '--portrait'; if (P) rest = rest.slice(1);
const W = P ? 1080 : 1920, H = P ? 1920 : 1080, MX = 475;   // MX: the safe box's centre line
const still = rest[0] === '--still' ? rest.slice(1).map(Number) : null;
const T = JSON.parse(fs.readFileSync(timelinePath));
const fps = T.fps, dur = T.duration, frames = Math.ceil(dur * fps), SEG = T.segments;
const clamp = (x, a = 0, b = 1) => Math.min(b, Math.max(a, x));
const ease = x => { x = clamp(x); return x * x * (3 - 2 * x); };
const GOLD = [236, 184, 92], TEAL = [104, 204, 190], IVORY = [246, 236, 218], SYMP = [214, 204, 186];
const rgba = (c, a) => `rgba(${c[0]},${c[1]},${c[2]},${clamp(a)})`;
const mixc = (a, b, f) => a.map((x, i) => Math.round(x + (b[i] - x) * f));
// Pieces change through a dip at the middle of their audio overlap, so two titles
// never sit on top of each other; the first piece fades in from its start.
const XF = 0.35;
SEG.forEach((s, i) => {
  s.in = i ? (s.start + SEG[i - 1].end) / 2 : s.start;
  s.out = i < SEG.length - 1 ? (s.end + SEG[i + 1].start) / 2 : Infinity;
  const span = s.end - s.start;
  for (const q of s.players) {
    q.shown = q.notes.filter(n => n[0] >= s.src0 - 0.02 && n[0] <= s.src0 + span);
    // the string each note sounded on, from the trace (promo_audio.py's strings_of)
    q.shownOn = q.notes.map((n, i) => [n, q.on[i]]).filter(([n]) => n[0] >= s.src0 - 0.02 && n[0] <= s.src0 + span);
  }
  s.last = Math.max(...s.players.flatMap(q => q.shown.map(n => n[1]))) - s.src0 + s.start;   // promo time of the last release
});
const segAlpha = (s, t) => ease((t - s.in) / XF) * ease((s.out - t) / XF);
const final = SEG[SEG.length - 1], OUT0 = final.last + 0.2;   // the end card rises as the last chord rings
// the string field: x0 the bridge saddle, x1 the nut; fret f sits 2^(-f/12) of the
// scale from the bridge; the pluck line over the sound hole, between fret 20 and the saddle
const R = P ? {x0: 40, x1: 1040, y0: 860, y1: 1640} : {x0: 960, x1: 1784, y0: 140, y1: 920};
const fretX = f => R.x0 + (R.x1 - R.x0) * Math.pow(2, -f / 12);
R.play = R.x0 + 0.17 * (R.x1 - R.x0);
const INLAYS = [3, 5, 7, 9, 12, 15, 17, 19];
const PPS = P ? 190 : 175;   // pixels per second of the notes riding in

function text(ctx, str, x, y, font, color, align = 'left', glow = null, sp = 0) {
  ctx.save(); ctx.font = font; ctx.fillStyle = color; ctx.textAlign = align;
  if (sp) ctx.letterSpacing = `${sp}px`;
  if (glow) { ctx.shadowColor = glow[0]; ctx.shadowBlur = glow[1]; }
  ctx.fillText(str, x, y); ctx.restore();
}
function round(ctx, x, y, w, h, r) {
  r = Math.min(r, h / 2, w / 2); ctx.beginPath(); ctx.moveTo(x + r, y); ctx.arcTo(x + w, y, x + w, y + h, r);
  ctx.arcTo(x + w, y + h, x, y + h, r); ctx.arcTo(x, y + h, x, y, r); ctx.arcTo(x, y, x + w, y, r); ctx.closePath();
}
const frameAt = (q, u) => clamp(Math.round(u * 60) - 1, 0, q.energy.length - 1);
const HEAD = P ? 14 : 30;   // above each player's band: its name (landscape), or a gap

// one piece's scene at piece time u, drawn at opacity a
function scene(ctx, s, u, a) {
  const total = s.players.reduce((x, q) => x + q.strings.length, 0);
  const gap = (R.y1 - R.y0 - HEAD * s.players.length) / total;
  ctx.save(); ctx.globalAlpha = a;
  let sounding = 0, sympathy = 0, top = R.y0;
  for (const q of s.players) {
    const n = q.strings.length, f = frameAt(q, u), y0 = top + HEAD, y1 = y0 + n * gap;
    const yOf = k => y1 - (k + 0.5) * gap;
    top = y1;
    if (!P) text(ctx, q.patch.toUpperCase(), R.x0, y0 - 9, '17px InterS', 'rgba(234,223,207,0.85)', 'left', null, 1.5);
    // the saddle, the frets with the panel's inlay dots, and the nut, as planar lines
    ctx.fillStyle = 'rgba(232,196,140,0.30)'; round(ctx, R.x0 - 10, y0 - 2, 6, y1 - y0 + 4, 3); ctx.fill();
    ctx.fillStyle = 'rgba(240,226,200,0.32)'; round(ctx, R.x1 + 2, y0 - 2, 6, y1 - y0 + 4, 2); ctx.fill();
    for (let fr = 1; fr <= 20; fr++) { ctx.fillStyle = 'rgba(214,190,150,0.13)'; ctx.fillRect(fretX(fr) - 0.75, y0, 1.5, y1 - y0); }
    for (const fr of INLAYS) {
      const x = (fretX(fr) + fretX(fr - 1)) / 2, ym = (y0 + y1) / 2;
      ctx.fillStyle = 'rgba(214,196,160,0.16)';
      for (const dy of fr === 12 ? [-gap, gap] : [0]) { ctx.beginPath(); ctx.arc(x, ym + dy, 3, 0, Math.PI * 2); ctx.fill(); }
    }
    for (let k = 0; k < n; k++) {
      const y = yOf(k), dB = q.energy[f][k], st = q.state[f][k], fret = q.fret[f][k];
      const lit = clamp((dB + 112) / 56);   // -112 dB dark .. -56 dB full
      if (st > 0 && dB > -110) sounding++; else if (st === 0 && dB > -100) sympathy++;
      const c = st === 2 ? GOLD : st === 1 ? TEAL : SYMP, base = st > 0 || dB > -100 ? c : [200, 170, 130];
      const xs = fret > 0 && st > 0 ? fretX(fret) : R.x1;   // the sounding length ends at the stopping fret
      const amp = Math.min(P ? 15 : 13, 0.9 * gap) * Math.pow(lit, 1.6), ph = Math.sin(2 * Math.PI * (2.1 + (k % 7) * 0.37) * u + k * 2.3);
      const wid = Math.min(1.4, gap / 12) * (1.0 + 0.45 * (n - 1 - k));   // the wound bass strings drawn heavier
      if (xs < R.x1) {   // nut side of the finger: still, unlit
        ctx.beginPath(); ctx.moveTo(xs, y); ctx.lineTo(R.x1, y); ctx.strokeStyle = 'rgba(150,120,92,0.30)'; ctx.lineWidth = wid; ctx.stroke();
      }
      for (const [lw, al] of [[wid + Math.min(6, gap * 0.4), 0.10 + 0.30 * lit], [wid, 0.22 + 0.78 * lit]]) {
        ctx.beginPath(); ctx.moveTo(R.x0, y);
        ctx.quadraticCurveTo((R.x0 + xs) / 2, y + 2 * amp * ph, xs, y);   // the vertex sits at amp
        ctx.strokeStyle = rgba(lit > 0.02 ? mixc(base, IVORY, 0.25 * lit) : [150, 120, 92], al); ctx.lineWidth = lw; ctx.stroke();
      }
      if (fret > 0 && st > 0) {   // the fingertip, just behind its fret
        ctx.fillStyle = rgba(st === 2 ? GOLD : TEAL, 0.55 + 0.45 * lit);
        ctx.beginPath(); ctx.arc(fretX(fret) + 0.35 * (fretX(fret - 1) - fretX(fret)), y, Math.min(7, gap * 0.3), 0, Math.PI * 2); ctx.fill();
      }
    }
    // the notes riding in on their string, plucked at the line
    const ahead = (R.x1 - R.play) / PPS, barH = clamp(gap * 0.5, 4, 12);
    for (const [nt, k] of q.shownOn) {
      const [st, e] = nt;
      if (st > u + ahead || st < u - 0.25) continue;
      const y = yOf(k);
      const xs = R.play + (st - u) * PPS, len = Math.max(barH, Math.min((e - st) * PPS, R.x1 - xs));
      const hit = clamp(1 - (u - st) / 0.25), edge = clamp((R.x1 - xs) / 60);
      ctx.save(); ctx.globalAlpha *= (st <= u ? hit : 0.85) * edge;
      if (st <= u) { ctx.shadowColor = rgba(GOLD, 0.9); ctx.shadowBlur = 18; }
      ctx.fillStyle = rgba(st <= u ? IVORY : GOLD, 1);
      const side = st <= u ? Math.max(barH, 9) : barH;   // the pluck flashes a little larger than its bar
      round(ctx, Math.max(R.play, xs), y - side / 2, st <= u ? side : Math.min(len, R.x1 - xs), side, side / 2); ctx.fill();
      ctx.restore();
    }
  }
  // the piece and its quartet
  const who = `${['', 'ONE GUITAR', 'TWO GUITARS', 'THREE GUITARS', 'FOUR GUITARS'][s.players.length]}  ·  ${s.how}`;
  if (P) {
    text(ctx, s.title, MX, 700, '84px Bask', '#f6ecdc', 'center', ['rgba(255,200,140,0.25)', 26]);
    text(ctx, s.sub, MX, 752, '30px InterS', '#e9b95e', 'center');
    text(ctx, s.how, MX, 800, '23px InterS', '#eadfcf', 'center', null, 0.5);
  } else {
    text(ctx, s.title, 136, 640, '96px Bask', '#f6ecdc', 'left', ['rgba(255,200,140,0.25)', 26]);
    text(ctx, s.sub, 140, 700, '38px InterS', '#e9b95e');
    text(ctx, who, 140, 790, '25px InterS', '#eadfcf', 'left', null, 1.5);
    ctx.save(); ctx.font = '25px InterR'; ctx.fillStyle = '#d9cbb6'; ctx.fillText(`${sounding} sounding  ·  `, 140, 834);
    const w0 = ctx.measureText(`${sounding} sounding  ·  `).width; ctx.fillStyle = rgba(SYMP, 1);
    ctx.fillText(`${sympathy} ringing in sympathy`, 140 + w0, 834); ctx.restore();
  }
  ctx.restore();
}

function draw(ctx, t) {
  const fade = ease(t / 0.5) * ease((dur - t) / 0.6);
  const live = SEG.map(s => [s, segAlpha(s, t)]).filter(([, a]) => a > 0);
  const endA = ease((t - OUT0) / 0.9);
  // ground: dark walnut, a warm glow behind the strings that rises with their energy
  const g = ctx.createLinearGradient(0, 0, W, H);
  g.addColorStop(0, '#120905'); g.addColorStop(0.5, '#21120a'); g.addColorStop(1, '#0d0603');
  ctx.fillStyle = g; ctx.fillRect(0, 0, W, H);
  let energy = 0;
  for (const [s, a] of live) energy += a * clamp((Math.max(...s.players.map(q => Math.max(...q.energy[frameAt(q, t - s.start + s.src0)]))) + 50) / 45);
  const cx = (R.x0 + R.x1) / 2, cy = (R.y0 + R.y1) / 2;
  const glow = ctx.createRadialGradient(cx, cy, 0, cx, cy, P ? 640 : 720);
  glow.addColorStop(0, `rgba(150,92,34,${(0.22 + 0.10 * clamp(energy)) * fade})`);
  glow.addColorStop(0.6, 'rgba(90,50,20,0.08)'); glow.addColorStop(1, 'rgba(0,0,0,0)');
  ctx.fillStyle = glow; ctx.fillRect(0, 0, W, H);
  for (let i = 0; i < 30; i++) {   // slow dust, deterministic
    const x = ((i * 397.31 + t * (5 + (i % 4) * 3)) % (W + 80)) - 40, y = (i * 211.7 + Math.sin(t * 0.19 + i) * 24 + H) % H;
    const r = 2 + (i % 5) * 1.3, a = (0.03 + (i % 3) * 0.022) * fade;
    const d = ctx.createRadialGradient(x, y, 0, x, y, r * 4);
    d.addColorStop(0, `rgba(255,222,170,${a})`); d.addColorStop(1, 'rgba(255,222,170,0)');
    ctx.fillStyle = d; ctx.fillRect(x - r * 4, y - r * 4, r * 8, r * 8);
  }
  for (const [s, a] of live) scene(ctx, s, t - s.start + s.src0, a * fade * (s === final ? 1 - endA : 1));
  ctx.fillStyle = `rgba(255,246,232,${0.7 * fade * (1 - endA)})`; ctx.fillRect(R.play - 1, R.y0 - 20, 2, R.y1 - R.y0 + 40);

  // the end card, over the strings
  if (endA > 0) {
    ctx.save(); ctx.globalAlpha = endA * fade; const mx = P ? MX : (R.x0 + R.x1) / 2, f = P ? 72 : 96;
    const y = P ? 1030 : 470, dy = P ? 92 : 120;
    text(ctx, 'No samples.', mx, y, `${f}px Bask`, '#f6ecdc', 'center', ['rgba(255,200,140,0.25)', 26]);
    text(ctx, 'Just strings.', mx, y + dy, `${f}px Bask`, '#f0c977', 'center', ['rgba(255,200,140,0.3)', 26]);
    if (!P) text(ctx, 'Eight factory patches · one physical model', mx, y + dy + 72, '36px InterS', '#e9b95e', 'center');
    ctx.restore();
  }

  // identity: on every frame, never faded
  if (P) {
    ctx.font = '28px InterS'; ctx.letterSpacing = '8px'; const pw = ctx.measureText('PROTOCODUS').width; ctx.letterSpacing = '0px';
    const lx = MX - (72 + 18 + pw) / 2;
    ctx.drawImage(logo, lx, 320, 72, 72); text(ctx, 'PROTOCODUS', lx + 90, 367, '28px InterS', '#efe4d4', 'left', null, 8);
  } else {
    ctx.drawImage(logo, 140, 108, 88, 88); text(ctx, 'PROTOCODUS', 248, 164, '30px InterS', '#efe4d4', 'left', null, 9);
  }
  // the panel's own ACUSTRA wordmark (Design/Brand/Acustra-wordmark.png), never retyped
  const wh = P ? 62 : 76, ww = wh * wordmark.width / wordmark.height, wy = P ? 432 : 238;
  ctx.save(); ctx.shadowColor = 'rgba(255,190,100,0.25)'; ctx.shadowBlur = 24;
  ctx.drawImage(wordmark, P ? MX - ww / 2 : 140, wy, ww, wh); ctx.restore();
  text(ctx, 'physically modelled acoustic guitar', P ? MX : 140, P ? 552 : 400, `${P ? 28 : 34}px InterM`, '#eadfd0', P ? 'center' : 'left', null, 1.2);
  text(ctx, `Reason Rack Extension · ${T.version}`, P ? MX : 140, P ? 588 : 446, `${P ? 22 : 25}px InterM`, 'rgba(222,190,124,0.92)', P ? 'center' : 'left', null, 0.6);

  // progress across the whole film, one track per piece
  const [px0, px1, py] = P ? [140, 940, 1700] : [140, 1784, 985], gp = 14;
  const bounds = SEG.map((s, i) => [i ? s.in : 0, Math.min(s.out, dur)]);
  ctx.save(); ctx.globalAlpha = fade;
  for (const [b0, b1] of bounds) {
    const x0 = px0 + (px1 - px0) * b0 / dur + (b0 > 0 ? gp / 2 : 0), x1 = px0 + (px1 - px0) * b1 / dur - (b1 < dur ? gp / 2 : 0);
    ctx.fillStyle = 'rgba(255,255,255,0.8)'; round(ctx, x0, py, x1 - x0, 5, 2.5); ctx.fill();
    const fr = clamp((t - b0) / (b1 - b0));
    if (fr > 0) { ctx.fillStyle = '#e8b55a'; round(ctx, x0, py, Math.max(5, (x1 - x0) * fr), 5, 2.5); ctx.fill(); }
  }
  ctx.restore();
}

const canvas = createCanvas(W, H), ctx = canvas.getContext('2d');
if (still) {
  for (const t of still) { ctx.clearRect(0, 0, W, H); draw(ctx, t); fs.writeFileSync(`${outPath}-${t}.png`, canvas.toBuffer('image/png')); }
} else {
  const ff = spawn('ffmpeg', ['-hide_banner', '-loglevel', 'error', '-y', '-f', 'rawvideo', '-pixel_format', 'rgba',
    '-video_size', `${W}x${H}`, '-framerate', String(fps), '-i', 'pipe:0', '-i', audioPath, '-map', '0:v', '-map', '1:a',
    '-c:v', 'libx264', '-preset', 'medium', '-crf', '18', '-profile:v', 'high', '-pix_fmt', 'yuv420p',
    '-colorspace', 'bt709', '-color_primaries', 'bt709', '-color_trc', 'bt709', '-color_range', 'tv',
    '-c:a', 'aac', '-b:a', '384k', '-ar', '48000', '-af', 'volume=-0.3dB', '-shortest', '-movflags', '+faststart', outPath],
    {stdio: ['pipe', 'inherit', 'inherit']});
  for (let i = 0; i < frames; i++) {
    draw(ctx, (i + 0.5) / fps);
    const buf = Buffer.from(ctx.getImageData(0, 0, W, H).data.buffer);
    if (!ff.stdin.write(buf)) await new Promise(r => ff.stdin.once('drain', r));
    if (i % 300 === 0) process.stderr.write(`${path.basename(outPath)} ${i}/${frames}\n`);
  }
  ff.stdin.end(); await new Promise(r => ff.on('close', r));
}
