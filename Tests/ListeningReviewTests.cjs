#!/usr/bin/env node
'use strict';

// Browser contracts, not a listening preference or a human approval.
// Usage:
//   node Tests/ListeningReviewTests.cjs --fixture
//   node Tests/ListeningReviewTests.cjs build-realism/final-listening-package
//   node Tests/ListeningReviewTests.cjs http://127.0.0.1:8000/ --artifacts build-realism/ui-check
// --fixture deliberately duplicates baseline recordings into BOTH versions.
// It tests the review application only and cannot demonstrate a realism gain.
// Requires an existing playwright-core installation and Chromium. No downloads.

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const http = require('node:http');
const crypto = require('node:crypto');
const { pathToFileURL } = require('node:url');
const { chromium } = require('playwright-core');

const root = path.resolve(__dirname, '..');
const args = process.argv.slice(2);
let target = null, artifactDirectory = path.join(root, 'build-realism', 'listening-ui-fixture', 'artifacts');
let makeFixture = false;
for (let i = 0; i < args.length; ++i) {
  if (args[i] === '--fixture') makeFixture = true;
  else if (args[i] === '--artifacts') { assert(args[i + 1], '--artifacts requires a directory'); artifactDirectory = path.resolve(args[++i]); }
  else if (!target) target = args[i];
  else throw new Error('Unexpected argument: ' + args[i]);
}
if (!target && !makeFixture) throw new Error('Pass a package directory or HTTP URL, or explicitly request --fixture.');

const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function waveDuration(bytes) {
  assert.equal(bytes.toString('ascii', 0, 4), 'RIFF');
  assert.equal(bytes.toString('ascii', 8, 12), 'WAVE');
  let byteRate = 0, dataBytes = 0;
  for (let offset = 12; offset + 8 <= bytes.length;) {
    const name = bytes.toString('ascii', offset, offset + 4), size = bytes.readUInt32LE(offset + 4);
    if (name === 'fmt ') byteRate = bytes.readUInt32LE(offset + 16);
    if (name === 'data') dataBytes = size;
    offset += 8 + size + (size & 1);
  }
  assert(byteRate > 0 && dataBytes > 0, 'Fixture recording needs a usable format and data chunk');
  return dataBytes / byteRate;
}
function fixture() {
  const directory = path.join(root, 'build-realism', 'listening-ui-fixture', 'package');
  fs.mkdirSync(path.join(directory, 'audio'), {recursive: true});
  const songs = [
    ['01-evening-fingerpicking', 'Evening fingerpicking'],
    ['02-open-road-strumming', 'Open road strumming'],
    ['03-connected-melody', 'Connected melody'],
    ['04-repeated-note-groove', 'Repeated-note groove'],
    ['05-resonance-and-body-transitions', 'Resonance and body transitions'],
  ];
  const data = {review_id: 'AUTOMATION-FIXTURE-identical-baseline-song-audio',
    baseline_commit: 'FIXTURE-baseline-audio-only', candidate_commit: 'FIXTURE-identical-baseline-copy',
    method: 'Browser automation fixture only. Both versions contain identical baseline audio. No realism comparison or human approval.', tracks: []};
  songs.forEach(([id, title], number) => {
    const track = {id, title, description: 'AUTOMATION FIXTURE: both versions duplicate the baseline recording. This is not a candidate comparison.', duration: 0, modes: {}, required: number < 4};
    for (const mode of ['dry', 'room']) {
      const source = path.join(root, 'build-realism', 'songs-baseline-' + mode, id + '.wav');
      assert(fs.existsSync(source), 'Render the baseline songs before using --fixture: ' + source);
      const bytes = fs.readFileSync(source); track.duration = waveDuration(bytes);
      const versions = ['x', 'y'].map(letter => {
        const file = `audio/${String(number + 1).padStart(2, '0')}-${mode}-${letter}.wav`;
        fs.copyFileSync(source, path.join(directory, file));
        // No loudness measurements are invented for this UI-only fixture.
        return {file, lufs: null, true_peak_db: null, gain_db: 0, sha256: sha256(bytes)};
      });
      track.modes[mode] = {versions, comparison: {equal_source_pcm: true, purpose: 'UI automation only'}};
    }
    data.tracks.push(track);
  });
  const template = fs.readFileSync(path.join(root, 'Tools', 'RealismListeningTemplate.html'), 'utf8');
  assert.equal(template.split('__REVIEW_DATA__').length - 1, 1);
  const notice = '<div style="padding:12px;text-align:center;background:#ffe6ad;color:#4b321b;font:12px system-ui" role="note">BROWSER AUTOMATION FIXTURE — both versions contain identical baseline recordings. This is not a realism comparison.</div>';
  fs.writeFileSync(path.join(directory, 'index.html'), template.replace('__REVIEW_DATA__', JSON.stringify(data).replace(/</g, '\\u003c')).replace('<body>', '<body>' + notice));
  fs.writeFileSync(path.join(directory, 'manifest.json'), JSON.stringify(data, null, 2) + '\n');
  fs.writeFileSync(path.join(directory, 'README.txt'), 'BROWSER AUTOMATION FIXTURE ONLY\n\nBoth A/B versions copy the same baseline recordings. The fixture cannot establish a realism improvement and its automated votes are not human listening or approval.\n');
  return directory;
}
function serve(directory) {
  const server = http.createServer((request, response) => {
    try {
      const pathname = decodeURIComponent(new URL(request.url, 'http://localhost').pathname);
      if (pathname === '/favicon.ico') { response.writeHead(204); response.end(); return; }
      const file = path.resolve(directory, '.' + (pathname === '/' ? '/index.html' : pathname));
      if (!file.startsWith(directory + path.sep) || !fs.existsSync(file) || !fs.statSync(file).isFile()) { response.writeHead(404); response.end('Not found'); return; }
      const types = {'.html': 'text/html; charset=utf-8', '.json': 'application/json', '.wav': 'audio/wav'};
      const size = fs.statSync(file).size, headers = {'Content-Type': types[path.extname(file)] || 'application/octet-stream', 'Content-Length': size, 'Cache-Control': 'no-store', 'Accept-Ranges': 'bytes'};
      const match = /^bytes=(\d+)-(\d*)$/.exec(request.headers.range || '');
      let start = 0, end = size - 1, status = 200;
      if (match) {
        start = Number(match[1]); end = match[2] ? Math.min(Number(match[2]), size - 1) : size - 1;
        if (start >= size || end < start) { response.writeHead(416, {'Content-Range': 'bytes */' + size}); response.end(); return; }
        status = 206; headers['Content-Range'] = `bytes ${start}-${end}/${size}`; headers['Content-Length'] = end - start + 1;
      }
      response.writeHead(status, headers);
      if (request.method === 'HEAD') response.end(); else fs.createReadStream(file, {start, end}).pipe(response);
    } catch (_) { response.writeHead(400); response.end('Bad request'); }
  });
  return new Promise((resolve, reject) => { server.once('error', reject); server.listen(0, '127.0.0.1', () => resolve({server, url: 'http://127.0.0.1:' + server.address().port + '/'})); });
}
function silentWave() {
  const sampleRate = 48000, frames = 4800, bytes = Buffer.alloc(44 + frames * 2);
  bytes.write('RIFF', 0); bytes.writeUInt32LE(bytes.length - 8, 4); bytes.write('WAVEfmt ', 8);
  bytes.writeUInt32LE(16, 16); bytes.writeUInt16LE(1, 20); bytes.writeUInt16LE(1, 22);
  bytes.writeUInt32LE(sampleRate, 24); bytes.writeUInt32LE(sampleRate * 2, 28);
  bytes.writeUInt16LE(2, 32); bytes.writeUInt16LE(16, 34); bytes.write('data', 36); bytes.writeUInt32LE(frames * 2, 40);
  return bytes;
}
async function installProbe(page) {
  await page.addInitScript(() => {
    const probe = window.__listeningProbe = {contexts: [], decoded: [], starts: [], ramps: []};
    const Native = window.AudioContext || window.webkitAudioContext;
    if (!Native) return;
    class ObservedAudioContext extends Native {
      constructor(...args) { super(...args); probe.contexts.push(this); }
      async decodeAudioData(bytes) { const buffer = await super.decodeAudioData(bytes); probe.decoded.push({duration: buffer.duration, frames: buffer.length, channels: buffer.numberOfChannels, sample_rate: buffer.sampleRate}); return buffer; }
      createBufferSource() {
        const source = super.createBufferSource(), start = source.start.bind(source);
        source.start = (when, offset, ...rest) => { probe.starts.push({when, offset, duration: source.buffer?.duration, frames: source.buffer?.length}); return start(when, offset, ...rest); };
        return source;
      }
      createGain() {
        const node = super.createGain(), ramp = node.gain.linearRampToValueAtTime.bind(node.gain), context = this;
        node.gain.linearRampToValueAtTime = (value, when) => { probe.ramps.push({value, when, now: context.currentTime}); return ramp(value, when); };
        return node;
      }
    }
    window.AudioContext = ObservedAudioContext;
  });
}
async function audioReady(page) {
  await page.waitForFunction(() => (!document.getElementById('play').disabled && document.getElementById('audio-status').textContent.includes('Both versions ready')) || document.getElementById('audio-status').textContent.includes('No listening pair is available'), null, {timeout: 30000});
  assert.equal(await page.locator('#play').isDisabled(), false, await page.locator('#audio-status').textContent());
}
async function position(page) { return Number(await page.locator('#seek').inputValue()); }
async function dataOf(page) { return page.locator('#review-data').evaluate(node => JSON.parse(node.textContent)); }
async function storageOf(page) { return page.evaluate(() => { const key = Object.keys(localStorage).find(key => key.startsWith('acustra.listening.v1:')); return key ? JSON.parse(localStorage.getItem(key)) : null; }); }
async function exportReview(page, name) {
  const downloading = page.waitForEvent('download'); await page.locator('#export').click();
  const download = await downloading, destination = path.join(artifactDirectory, name);
  await download.saveAs(destination); return JSON.parse(fs.readFileSync(destination, 'utf8'));
}
function checkScheduledPair(starts) {
  const pair = starts.slice(-2); assert.equal(pair.length, 2);
  assert(Math.abs(pair[0].when - pair[1].when) < 1e-9, 'Both decoded sources must start on the same AudioContext sample clock');
  assert(Math.abs(pair[0].offset - pair[1].offset) < 1e-9, 'Both decoded sources must seek to the same passage position');
  assert(pair.every(source => source.frames > 1000 && source.duration > 1), 'The browser must play real decoded recordings');
}

(async () => {
  fs.mkdirSync(artifactDirectory, {recursive: true});
  const report = {purpose: 'Browser automation only; no human realism preference or approval.', target: null, fixture: makeFixture, checks: [], page_errors: [], console_errors: [], started_at: new Date().toISOString()};
  let server = null, browser = null, packageDirectory = null;
  const pass = name => { report.checks.push({name, status: 'PASS'}); console.log('PASS ' + name); };
  try {
    if (makeFixture) { assert(!target, 'Use either --fixture or an existing package'); target = fixture(); }
    let url;
    if (/^https?:\/\//.test(target)) url = target;
    else { packageDirectory = path.resolve(target); assert(fs.existsSync(path.join(packageDirectory, 'index.html')), 'The package directory needs index.html'); const serving = await serve(packageDirectory); server = serving.server; url = serving.url; }
    report.target = packageDirectory || 'supplied HTTP package';
    browser = await chromium.launch({executablePath: process.env.CHROMIUM_PATH || '/usr/bin/chromium', headless: true, args: ['--no-sandbox', '--disable-dev-shm-usage']});
    const context = await browser.newContext({viewport: {width: 1280, height: 980}, acceptDownloads: true});
    const page = await context.newPage(); await installProbe(page);
    page.on('pageerror', error => report.page_errors.push(error.message));
    page.on('console', message => { if (message.type() === 'error' && !message.location().url.endsWith('/favicon.ico')) report.console_errors.push({message: message.text(), url: message.location().url}); });
    await page.goto(url); await audioReady(page); const data = await dataOf(page);
    report.review_id = data.review_id;
    report.package_content_sha256 = data.package_content_sha256 || null;
    if (packageDirectory) report.manifest_sha256 = sha256(fs.readFileSync(path.join(packageDirectory, 'manifest.json')));
    assert(data.tracks.length >= 4); const initialStorage = await storageOf(page);
    assert.equal(initialStorage.approval, null);
    assert.equal(await page.locator('#reveal').isDisabled(), true);
    assert.equal(await page.locator('[data-decision="approve_candidate"]').isDisabled(), true);
    assert.equal(await page.locator('#reveal-panel').isVisible(), false);
    const visibleText = await page.locator('body').innerText();
    for (const secret of [data.baseline_commit, data.candidate_commit, data.tracks[0].description, ...data.tracks[0].modes.dry.versions.map(version => version.file)]) if (secret) assert(!visibleText.includes(secret), 'Version identities, descriptions and filenames must stay out of the visible blind interface');
    assert.equal((await page.evaluate(() => window.__listeningProbe.decoded.length)), 2);
    pass('HTTP decoding, hidden identities, and four-vote reveal/approval gate');
    await page.screenshot({path: path.join(artifactDirectory, 'desktop-blind.png'), fullPage: true});

    await page.locator('#play').click();
    await page.waitForFunction(() => window.__listeningProbe.starts.length === 2);
    checkScheduledPair(await page.evaluate(() => window.__listeningProbe.starts));
    await page.waitForFunction(() => Number(document.getElementById('seek').value) > 0.20);
    const beforeSwitch = await position(page); await page.locator('#version-b').click();
    const afterSwitch = await position(page); assert(afterSwitch >= beforeSwitch && afterSwitch - beforeSwitch < 0.5, 'A/B switch must keep playback position');
    const ramps = await page.evaluate(() => window.__listeningProbe.ramps);
    assert.equal(ramps.length, 2); assert.deepEqual(ramps.map(r => r.value).sort(), [0, 1]);
    ramps.forEach(ramp => assert(Math.abs(ramp.when - ramp.now - 0.010) < 1e-6, 'A/B gain crossfade must last 10 ms'));
    assert.equal((await page.evaluate(() => window.__listeningProbe.starts.length)), 2, 'A/B must not restart or replace source nodes');
    pass('Simultaneous source scheduling and gain-only 10 ms A/B switching');

    await page.evaluate(() => document.activeElement.blur()); await page.keyboard.press('a');
    assert.equal(await page.locator('#version-a').getAttribute('aria-pressed'), 'true');
    await page.keyboard.press('Space'); assert.equal(await page.locator('#play').getAttribute('aria-label'), 'Play passage');
    await page.keyboard.press('Space'); await page.waitForFunction(() => window.__listeningProbe.starts.length === 4);
    checkScheduledPair(await page.evaluate(() => window.__listeningProbe.starts));
    await page.keyboard.press('b'); assert.equal(await page.locator('#version-b').getAttribute('aria-pressed'), 'true');
    pass('Keyboard A/B and Space playback');

    await page.locator('#seek').evaluate(node => { node.value = '3'; node.dispatchEvent(new Event('input', {bubbles: true})); });
    await page.waitForFunction(() => window.__listeningProbe.starts.length === 6);
    const seekStarts = await page.evaluate(() => window.__listeningProbe.starts); checkScheduledPair(seekStarts); assert(Math.abs(seekStarts.at(-1).offset - 3) < 0.02);
    const beforeMode = await position(page), previousCount = seekStarts.length;
    await page.locator('[data-mode="room"]').click();
    await page.waitForFunction(count => window.__listeningProbe.starts.length === count + 2 && document.getElementById('play').getAttribute('aria-label') === 'Pause passage', previousCount);
    const modeStarts = await page.evaluate(() => window.__listeningProbe.starts); checkScheduledPair(modeStarts);
    assert(Math.abs(modeStarts.at(-1).offset - beforeMode) < 0.5, 'Changing the room must resume from the same passage');
    assert.equal(await page.locator('[data-mode="room"]').getAttribute('aria-pressed'), 'true');
    assert.equal((await page.evaluate(() => window.__listeningProbe.decoded.length)), 4);
    await page.locator('#play').click();
    pass('Synchronized seek and dry/studio mode changes');

    await page.reload(); await audioReady(page);
    const reloaded = await storageOf(page); assert.deepEqual(reloaded.mappings, initialStorage.mappings);
    assert.equal(await page.locator('[data-mode="room"]').getAttribute('aria-pressed'), 'true');
    assert.equal(await page.locator('#play').getAttribute('aria-label'), 'Play passage');
    assert.equal(await page.evaluate(() => window.__listeningProbe.starts.length), 0);
    pass('Reload preserves blind mapping and mode without autoplay');

    for (let i = 0; i < 4; ++i) {
      if (i > 0) { await page.locator('[data-track]').nth(i).click(); await audioReady(page); }
      assert.equal(await page.locator('#save-vote').isDisabled(), true, 'Both naturalness scores and a preference are required');
      await page.locator('input[name="preference"][value="' + ['none', 'A', 'B', 'A'][i] + '"]').check();
      await page.locator('#score-a').selectOption(String(4 + i % 3));
      assert.equal(await page.locator('#save-vote').isDisabled(), true, 'One score is insufficient');
      await page.locator('#score-b').selectOption(String(3 + i % 3));
      await page.locator('#comment').fill('AUTOMATED_BROWSER_TEST_NOT_USER_APPROVAL — passage ' + (i + 1));
      await page.locator('#save-vote').click();
      assert.equal(await page.locator('#progress-label').textContent(), (i + 1) + ' of 4 passages reviewed');
      assert.equal(await page.locator('#reveal').isDisabled(), i < 3);
      assert.equal(await page.locator('[data-decision="approve_candidate"]').isDisabled(), true);
      if (i === 0) {
        const partial = await exportReview(page, 'AUTOMATION-only-partial-review.json');
        assert.equal(partial.explicit_decision, null); assert.equal(partial.main_passages_completed, 1);
        assert(partial.tracks.every(track => track.mapping === null), 'Every source mapping stays hidden until explicit Reveal, including voted passages');
        assert.equal(Object.hasOwn(partial.tracks[0].result, 'preferred_source'), false);
        assert.equal(Object.hasOwn(partial.tracks[0].result, 'naturalness_by_source'), false);
        await page.reload(); await audioReady(page);
        assert.equal(await page.locator('#score-a').inputValue(), '4'); assert.equal(await page.locator('#score-b').inputValue(), '3');
        assert((await page.locator('#comment').inputValue()).includes('AUTOMATED_BROWSER_TEST_NOT_USER_APPROVAL'));
        assert.deepEqual((await storageOf(page)).mappings, initialStorage.mappings);
      }
    }
    pass('Votes, both scores, comments, timestamp persistence, and partial export blinding');
    const fullBlind = await exportReview(page, 'AUTOMATION-only-complete-unrevealed-review.json');
    assert.equal(fullBlind.identities_revealed, false); assert.equal(fullBlind.main_passages_completed, 4);
    assert(fullBlind.tracks.every(track => track.mapping === null));
    assert(fullBlind.tracks.every(track => !track.result || (!Object.hasOwn(track.result, 'preferred_source') && !Object.hasOwn(track.result, 'naturalness_by_source'))), 'Source-decoded votes and scores must be absent before explicit Reveal');
    pass('Complete blind export preserves A/B secrecy until explicit Reveal');
    await page.locator('#reveal').click(); assert.equal(await page.locator('#reveal-panel').isVisible(), true);
    assert.equal(await page.locator('#baseline-commit').textContent(), data.baseline_commit);
    assert.equal(await page.locator('#candidate-commit').textContent(), data.candidate_commit);
    assert.equal((await storageOf(page)).approval, null, 'Reveal must never automatically approve');
    await page.locator('[data-decision="approve_candidate"]').click();
    const exported = await exportReview(page, 'AUTOMATION-only-approved-review-NOT-HUMAN-APPROVAL.json');
    assert.equal(exported.baseline_commit, data.baseline_commit); assert.equal(exported.candidate_commit, data.candidate_commit);
    assert.equal(exported.main_passages_completed, 4); assert.equal(exported.identities_revealed, true);
    assert.equal(exported.explicit_decision.decision, 'approve_candidate'); assert(Number.isFinite(Date.parse(exported.explicit_decision.timestamp)));
    assert(Number.isFinite(Date.parse(exported.revealed_at))); assert(Number.isFinite(Date.parse(exported.exported_at)));
    assert.equal(exported.playback.method, 'simultaneously_scheduled_web_audio'); assert.equal(exported.playback.switching_crossfade_ms, 10);
    for (let i = 0; i < 4; ++i) {
      const track = exported.tracks[i], saved = (await storageOf(page)).decisions[track.id];
      assert(track.mapping.A !== track.mapping.B); assert.equal(track.result.mode, 'room'); assert.equal(track.result.blind_at_vote, true);
      assert(Number.isFinite(Date.parse(track.result.timestamp))); assert.deepEqual(track.modes, data.tracks[i].modes);
      assert.equal(track.result.preference, saved.preference); assert.equal(track.result.naturalness_by_source.baseline, track.result.naturalness[track.mapping.A === 'baseline' ? 'A' : 'B']);
      assert.equal(track.result.naturalness_by_source.candidate, track.result.naturalness[track.mapping.A === 'candidate' ? 'A' : 'B']);
      assert.equal(track.result.preferred_source, track.result.preference === 'none' ? 'no_preference' : track.mapping[track.result.preference]);
    }
    if (exported.tracks.length > 4) { assert.equal(exported.tracks[4].required, false); assert.equal(exported.tracks[4].result, null); }
    pass('Explicit decision and complete JSON evidence export');
    await page.screenshot({path: path.join(artifactDirectory, 'desktop-revealed-AUTOMATION.png'), fullPage: true});

    await page.locator('#comment').fill('AUTOMATED_BROWSER_TEST_NOT_USER_APPROVAL — amended vote'); await page.locator('#save-vote').click();
    assert.equal((await storageOf(page)).approval, null, 'An updated vote must invalidate an earlier approval');
    await page.locator('[data-decision="request_revision"]').click();
    await page.reload(); await audioReady(page);
    assert.equal(await page.locator('[data-decision="request_revision"]').getAttribute('aria-pressed'), 'true');
    assert.equal((await storageOf(page)).approval.decision, 'request_revision');
    assert.deepEqual((await storageOf(page)).mappings, initialStorage.mappings);
    pass('Changed votes invalidate approval; explicit revision decision survives reload');

    await page.setViewportSize({width: 375, height: 812});
    await page.screenshot({path: path.join(artifactDirectory, 'mobile-375-AUTOMATION.png'), fullPage: true});
    report.mobile_layout = await page.evaluate(() => ({viewport: innerWidth, document_width: document.documentElement.scrollWidth, overflowing: [...document.body.querySelectorAll('*')].filter(node => node.getBoundingClientRect().right > innerWidth + 1 || node.scrollWidth > node.clientWidth + 1).map(node => ({tag: node.tagName, id: node.id, class: String(node.className), right: node.getBoundingClientRect().right, width: node.getBoundingClientRect().width, content_width: node.scrollWidth})).slice(0, 20)}));
    assert(report.mobile_layout.document_width <= report.mobile_layout.viewport, '375 px layout must not overflow horizontally: ' + JSON.stringify(report.mobile_layout));
    assert((await page.locator('#play').boundingBox()).width >= 32);
    pass('375 px responsive layout');

    for (const [name, response] of [['missing', {status: 404, contentType: 'text/plain', body: 'Intentionally missing test audio'}], ['silent', {status: 200, contentType: 'audio/wav', body: silentWave()}]]) {
      const failingContext = await browser.newContext({viewport: {width: 1100, height: 800}}), failingPage = await failingContext.newPage();
      const audioURL = new URL(data.tracks[0].modes.dry.versions[0].file, url).href;
      await failingPage.route(audioURL, route => route.fulfill(response));
      await failingPage.goto(url); await failingPage.waitForFunction(() => document.getElementById('audio-status').textContent.includes('No listening pair is available'));
      assert.equal(await failingPage.locator('#play').isDisabled(), true); assert.equal(await failingPage.locator('#save-vote').isDisabled(), true);
      assert.equal(await failingPage.locator('#reveal').isDisabled(), true);
      if (name === 'silent') assert((await failingPage.locator('#audio-status').textContent()).includes('silent'));
      await failingPage.screenshot({path: path.join(artifactDirectory, name + '-audio-error.png'), fullPage: true});
      await failingContext.close(); pass(name + ' audio disables playback and voting');
    }
    if (packageDirectory) {
      const fileContext = await browser.newContext({viewport: {width: 1100, height: 800}}), filePage = await fileContext.newPage(); await installProbe(filePage);
      try {
        await filePage.goto(pathToFileURL(path.join(packageDirectory, 'index.html')).href); await audioReady(filePage);
        assert((await filePage.locator('#audio-status').textContent()).includes('local preview server'));
        assert.equal(await filePage.evaluate(() => window.__listeningProbe.contexts.length), 0);
        await filePage.locator('#play').click(); await filePage.waitForFunction(() => Number(document.getElementById('seek').value) > 0.15);
        const previous = await position(filePage); await filePage.locator('#version-b').click(); const next = await position(filePage);
        assert(next >= previous - 0.05 && next - previous < 0.5, 'File-opening fallback must keep the same passage');
        await filePage.locator('#play').click(); pass('Direct-file media-element fallback playback and aligned switching');
      } catch (error) {
        if (!error.message.includes('ERR_BLOCKED_BY_ADMINISTRATOR')) throw error;
        report.checks.push({name: 'Direct-file navigation', status: 'SKIP', reason: 'Managed Chromium policy blocks file: navigation. The shared media-element fallback is tested separately over HTTP.'});
        console.log('SKIP Direct-file navigation: managed Chromium policy blocks file: URLs');
      } finally { await fileContext.close(); }
    }
    // Test the same fallback path without weakening the host's file-URL policy.
    const mediaContext = await browser.newContext({viewport: {width: 1100, height: 800}}), mediaPage = await mediaContext.newPage();
    await mediaPage.addInitScript(() => { window.AudioContext = undefined; window.webkitAudioContext = undefined; const NativeAudio = window.Audio; window.__mediaProbe = []; window.Audio = function(...args) { const audio = new NativeAudio(...args); window.__mediaProbe.push(audio); return audio; }; });
    await mediaPage.goto(url);
    try { await audioReady(mediaPage); } catch (error) { await mediaPage.screenshot({path: path.join(artifactDirectory, 'fallback-load-error.png'), fullPage: true}); report.media_diagnostic = await mediaPage.evaluate(() => window.__mediaProbe.map(audio => ({src: audio.currentSrc, duration: String(audio.duration), ready_state: audio.readyState, network_state: audio.networkState, error: audio.error && {code: audio.error.code, message: audio.error.message}}))); throw error; }
    assert((await mediaPage.locator('#audio-status').textContent()).includes('local preview server'));
    await mediaPage.locator('#play').click(); await mediaPage.waitForFunction(() => Number(document.getElementById('seek').value) > 0.15);
    const beforeMediaSwitch = await position(mediaPage); await mediaPage.locator('#version-b').click(); const afterMediaSwitch = await position(mediaPage);
    report.media_switch_diagnostic = {before: beforeMediaSwitch, after: afterMediaSwitch, elements: await mediaPage.evaluate(() => window.__mediaProbe.map(audio => ({current_time: audio.currentTime, duration: String(audio.duration), paused: audio.paused, seeking: audio.seeking, ready_state: audio.readyState, volume: audio.volume})))};
    assert(afterMediaSwitch >= beforeMediaSwitch - 0.05 && afterMediaSwitch - beforeMediaSwitch < 0.5, 'Media-element fallback must keep the same passage: ' + JSON.stringify(report.media_switch_diagnostic));
    await mediaPage.locator('#play').click(); await mediaContext.close(); pass('Media-element fallback playback and aligned switching with Web Audio unavailable');
    assert.deepEqual(report.page_errors, [], 'No uncaught errors are allowed in the normal review');
    assert.deepEqual(report.console_errors, [], 'No console errors are allowed in the normal HTTP review');
    pass('Normal review has no browser errors');
    await context.close(); report.status = 'PASS';
  } catch (error) {
    report.status = 'FAIL'; report.error = error.stack; console.error(error.stack); process.exitCode = 1;
  } finally {
    if (browser) await browser.close(); if (server) await new Promise(resolve => server.close(resolve));
    report.finished_at = new Date().toISOString(); fs.writeFileSync(path.join(artifactDirectory, 'browser-test-report.json'), JSON.stringify(report, null, 2) + '\n');
    fs.writeFileSync(path.join(artifactDirectory, 'browser-test.log'), [report.purpose, 'Overall: ' + report.status, ...report.checks.map(check => check.status + ' ' + check.name + (check.reason ? ' — ' + check.reason : '')), ...(report.error ? [report.error] : []), 'Started: ' + report.started_at, 'Finished: ' + report.finished_at].join('\n') + '\n');
    console.log('Artifacts: ' + artifactDirectory);
  }
})();
