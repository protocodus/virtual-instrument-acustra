// Thumbnails for the Acustra promo and its shorts, after Aculyre's thumbs.mjs.
// Hero art: Codex image generation on the owner's Mac, 2026-11-11, prompts in
// landscape.prompt.txt and portrait.prompt.txt beside the images in ../assets
// (checked: six strings, six bridge pins, six tuners, three a side, no
// maker's mark). Three landscape concepts (3840x2160 masters; lower right
// kept clear for the duration label): A and B on the hero art with different
// hooks, C on a frame of the film itself. Portrait: a 1080x1920 cover per
// short, words inside the project's TikTok safe box (x 130-821, y 307-1190).
// usage: node thumbs.mjs <outdir> <film-frame.png>
import fs from 'node:fs';
import path from 'node:path';
import {createRequire} from 'node:module';
const require = createRequire(import.meta.url);
const {createCanvas, loadImage, GlobalFonts} = require('@napi-rs/canvas');
const A = path.resolve('../assets'), [out, framePath] = process.argv.slice(2);
GlobalFonts.registerFromPath(`${A}/LibreBaskerville-wght.ttf`, 'Bask');
GlobalFonts.registerFromPath('/usr/share/fonts/opentype/inter/Inter-SemiBold.otf', 'InterS');
GlobalFonts.registerFromPath('/usr/share/fonts/opentype/inter/Inter-ExtraBold.otf', 'InterX');
const logo = await loadImage(`${A}/logo.png`);
const heroL = await loadImage(`${A}/landscape.png`), heroP = await loadImage(`${A}/portrait.png`);
const frame = await loadImage(framePath);
const VERSION = '1.9.41d2';

function gold(ctx, y, size) {
  const g = ctx.createLinearGradient(0, y - size * 0.8, 0, y + size * 0.15);
  g.addColorStop(0, '#fbe4a8'); g.addColorStop(0.55, '#e9b862'); g.addColorStop(1, '#c48a3a'); return g;
}
function line(ctx, str, x, y, font, fill, align = 'left', glow = null, stroke = 0, sp = 0) {
  ctx.save(); ctx.font = font; ctx.textAlign = align; ctx.fillStyle = fill;
  if (sp) ctx.letterSpacing = `${sp}px`;
  if (stroke) { ctx.lineJoin = 'round'; ctx.lineWidth = stroke; ctx.strokeStyle = 'rgba(14,7,3,0.85)'; ctx.strokeText(str, x, y); }
  if (glow) { ctx.shadowColor = glow[0]; ctx.shadowBlur = glow[1]; }
  ctx.fillText(str, x, y); ctx.restore();
}
function identity(ctx, x, y, s, align = 'left') {
  ctx.save(); ctx.font = `${30 * s}px InterS`;
  const label = `PROTOCODUS  ·  ACUSTRA ${VERSION}`, tw = ctx.measureText(label).width, lw = 64 * s;
  const lx = align === 'center' ? x - (lw + 16 * s + tw) / 2 : x;
  ctx.drawImage(logo, lx, y - 48 * s, lw, lw); ctx.fillStyle = '#f2e7d7'; ctx.fillText(label, lx + lw + 16 * s, y);
  ctx.restore();
}
function hero(ctx, img, k) {   // the art scaled to the frame's width, its dark left half under the words
  const hw = 1920 * k, hh = hw * img.height / img.width;
  ctx.fillStyle = '#0d0703'; ctx.fillRect(0, 0, 1920, 1080);
  ctx.drawImage(img, 1920 - hw, (1080 - hh) / 2, hw, hh);
}

// Landscape, drawn at 1920x1080 units and scaled 2x for the 4K master.
function landscape(file, paint, kicker, hook1, hook2, colour2 = '#f6ead6') {
  const S = 2, cv = createCanvas(1920 * S, 1080 * S), ctx = cv.getContext('2d');
  ctx.scale(S, S); paint(ctx);
  identity(ctx, 120, 150, 1);
  line(ctx, kicker, 120, 250, '42px InterX', '#ebb95e', 'left', null, 8, 2);
  line(ctx, hook1, 112, 410, '124px Bask', gold(ctx, 410, 124), 'left', ['rgba(255,186,90,0.45)', 40], 14);
  if (hook2) line(ctx, hook2, 112, 548, '124px Bask', colour2, 'left', ['rgba(255,200,140,0.3)', 30], 14);
  fs.writeFileSync(`${out}/${file}-3840x2160.png`, cv.toBuffer('image/png'));
}
function portrait(file, kicker, hook1, hook2) {
  const W = 1080, H = 1920, cv = createCanvas(W, H), ctx = cv.getContext('2d'), MX = 475;
  // the art's guitar stands small in its lower half: crop to it, so it fills the
  // cover's lower part and its headstock stays under the words (y > 790)
  const sw = 0.77 * heroP.width, sh = sw * H / W;
  ctx.drawImage(heroP, (heroP.width - sw) / 2, heroP.height - sh, sw, sh, 0, 0, W, H);
  identity(ctx, MX, 370, 0.78, 'center');
  line(ctx, kicker, MX, 470, '32px InterX', '#ebb95e', 'center', null, 6, 1.5);
  line(ctx, hook1, MX, 612, '100px Bask', gold(ctx, 612, 100), 'center', ['rgba(255,186,90,0.45)', 34], 10);
  if (hook2) line(ctx, hook2, MX, 726, '72px Bask', '#f6ead6', 'center', ['rgba(255,200,140,0.3)', 26], 10);
  fs.writeFileSync(`${out}/${file}.png`, cv.toBuffer('image/png'));
}
landscape('promo-thumb-A', ctx => hero(ctx, heroL, 1.0), 'FOUR GUITARS · THREE DANCES', 'No samples.', 'Just strings.');
landscape('promo-thumb-B', ctx => hero(ctx, heroL, 1.0), 'A STEEL-STRING GUITAR, MODELLED', 'Six strings,', 'all computed.');
landscape('promo-thumb-C', ctx => {
  // the film's own frame: four guitars' strings, held gold, released teal
  // (its words and progress bar covered: the strings start at x 950, end at y 920)
  ctx.drawImage(frame, 0, 0, 1920, 1080);
  const f = ctx.createLinearGradient(0, 0, 1000, 0); f.addColorStop(0, 'rgba(15,8,4,1)'); f.addColorStop(0.9, 'rgba(15,8,4,1)'); f.addColorStop(1, 'rgba(15,8,4,0)');
  ctx.fillStyle = f; ctx.fillRect(0, 0, 1000, 1080);
  const b = ctx.createLinearGradient(0, 925, 0, 960); b.addColorStop(0, 'rgba(15,8,4,0)'); b.addColorStop(1, 'rgba(15,8,4,1)');
  ctx.fillStyle = b; ctx.fillRect(0, 925, 1920, 155);
}, 'NO SAMPLES · JUST STRINGS', 'Four guitars,', 'one model.', '#7fd6c8');
portrait('short-minuet-cover', 'NO SAMPLES · JUST STRINGS', 'Minuet in G', 'for four guitars');
portrait('short-kesh-cover', 'NO SAMPLES · JUST STRINGS', 'The Kesh', 'a jig for four guitars');
portrait('short-butterfly-cover', 'NO SAMPLES · JUST STRINGS', 'The Butterfly', 'a slip jig for four guitars');
