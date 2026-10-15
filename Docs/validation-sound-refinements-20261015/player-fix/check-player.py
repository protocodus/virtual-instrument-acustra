import asyncio,json,http.server,threading,functools,time
from pathlib import Path
from playwright.async_api import async_playwright
ROOT=Path('/workspace/acustra-realism-20261015/listening-fixed')
class Quiet(http.server.SimpleHTTPRequestHandler):
 def log_message(self,*args): pass
class Server(http.server.ThreadingHTTPServer):
 def handle_error(self,*args): pass
server=Server(('127.0.0.1',0),functools.partial(Quiet,directory=str(ROOT)))
threading.Thread(target=server.serve_forever,daemon=True).start()
report={'url':f'http://127.0.0.1:{server.server_port}/index.html','server_supports_range':False,'checks':[]}
def check(name,condition,detail):
 report['checks'].append({'name':name,'pass':bool(condition),'detail':detail})
 if not condition: raise AssertionError((name,detail))
async def state(page,i=0):
 return await page.evaluate('(i)=>{const s=states[i];return {t:s.audio.currentTime,position:s.position,paused:s.audio.paused,playing:s.playing,loading:s.loading,status:s.status.textContent,src:s.audio.src,seekable:s.audio.seekable.length? s.audio.seekable.end(0):null}}',i)
async def main():
 async with async_playwright() as p:
  browser=await p.chromium.launch(executable_path='/usr/bin/chromium',headless=True,args=['--no-sandbox','--autoplay-policy=no-user-gesture-required'])
  page=await browser.new_page(); errors=[]; requests=[]
  page.on('pageerror',lambda e:errors.append(str(e)))
  page.on('request',lambda r:requests.append(r.url) if '.wav' in r.url else None)
  await page.goto(report['url']); await page.wait_for_timeout(100)
  check('lazy_loading_no_initial_audio_downloads',not requests,requests)
  for i in range(10):
   row=page.locator('section[data-scene]').nth(i)
   await row.locator('.play').click()
   await page.wait_for_function('(i)=>states[i].playing && !states[i].loading && states[i].audio.currentTime > 0.1',arg=i)
   result=await state(page,i)
   check(f'scene_{i+1}_plays_pcm24',result['src'].startswith('blob:') and result['seekable']==10,result)
  first=page.locator('section[data-scene]').first
  await first.locator('.play').click(); await page.wait_for_function('states[0].playing && !states[0].loading')
  await first.locator('.seek').evaluate('(el)=>{el.value=5;el.dispatchEvent(new Event("input"))}')
  await page.wait_for_timeout(100)
  check('seek_works_without_http_ranges',(await state(page))['t']>=5,await state(page))
  await first.locator('.side[data-side=B]').click(); await page.wait_for_function('states[0].playing && !states[0].loading')
  check('switch_side_preserves_position',(await state(page))['t']>=5,await state(page))
  await first.locator('.mode').select_option('native'); await page.wait_for_function('states[0].playing && !states[0].loading')
  check('switch_level_preserves_position',(await state(page))['t']>=5,await state(page))
  await first.locator('.play').click()
  await first.locator('.seek').evaluate('(el)=>{el.value=7;el.dispatchEvent(new Event("input"))}')
  await first.locator('.side[data-side=A]').click()
  await first.locator('.side[data-side=B]').click()
  await first.locator('.play').click(); await page.wait_for_function('states[0].playing && !states[0].loading')
  check('paused_switch_keeps_requested_position',(await state(page))['t']>=7,await state(page))
  await first.locator('.restart').click(); await page.wait_for_timeout(100)
  check('restart_keeps_playing_from_start',0<(await state(page))['t']<.5 and not (await state(page))['paused'],await state(page))
  second=page.locator('section[data-scene]').nth(1)
  await second.locator('.play').click(); await page.wait_for_function('states[1].playing && !states[1].loading')
  check('scene_exclusivity',await page.evaluate('states.flatMap(s=>Object.values(s.players)).filter(a=>!a.paused).length===1'),await state(page))
  await first.locator('.vote[data-vote=A]').click(); await first.locator('.notes').fill('review note')
  async with page.expect_download() as dl: await page.locator('#export').click()
  download=await dl.value; data=json.loads(Path(await download.path()).read_text())
  check('vote_export_keeps_pack_and_notes',data['answers']['01-soft-register']['notes']=='review note' and data['pack_id']==json.loads((ROOT/'manifest.json').read_text())['pack_id'],{'pack_id':data['pack_id']})
  await page.locator('#reveal').click()
  check('key_reveal',await page.locator('#key iframe').count()==1,'iframe present')
  check('no_script_errors',not errors,errors)
  await page.close()
  page=await browser.new_page(); errors=[]
  page.on('pageerror',lambda e:errors.append(str(e)))
  async def delayed(route):
   await asyncio.sleep(.8)
   await route.continue_()
  await page.route('**/*.wav',delayed)
  await page.goto(report['url']); first=page.locator('section[data-scene]').first
  await first.locator('.play').click()
  await first.locator('.seek').evaluate('(el)=>{el.value=6;el.dispatchEvent(new Event("input"))}')
  await first.locator('.side[data-side=B]').click()
  await page.wait_for_function('states[0].playing && !states[0].loading')
  result=await state(page)
  check('seek_and_switch_during_delayed_load',result['t']>=6 and 'B' in result['status'] and await page.evaluate('states.flatMap(s=>Object.values(s.players)).filter(a=>!a.paused).length===1'),result)
  second=page.locator('section[data-scene]').nth(1)
  await second.locator('.play').click(); await second.locator('.play').click(); await page.wait_for_timeout(1000)
  check('cancel_during_delayed_load_no_late_play',await page.evaluate('states.flatMap(s=>Object.values(s.players)).every(a=>a.paused)'),await state(page,1))
  check('no_delayed_script_errors',not errors,errors)
  await page.close()
  page=await browser.new_page()
  async def missing(route): await route.fulfill(status=404,body='missing')
  await page.route('**/01-soft-register-A.wav',missing)
  await page.goto(report['url']); first=page.locator('section[data-scene]').first
  await first.locator('.play').click(); await page.wait_for_function('!states[0].playing')
  result=await state(page)
  check('missing_audio_error_is_actionable','HTTP 404' in result['status'] and 'hosted' in result['status'],result)
  await page.unroute('**/01-soft-register-A.wav')
  await first.locator('.play').click(); await page.wait_for_function('states[0].playing && !states[0].loading')
  check('retry_after_missing_audio',not(await state(page))['paused'],await state(page))
  await page.close()
  page=await browser.new_page()
  async def html_fallback(route): await route.fulfill(status=200,content_type='text/html',body='<html>preview</html>')
  await page.route('**/01-soft-register-A.wav',html_fallback)
  await page.goto(report['url']); await page.locator('section[data-scene]').first.locator('.play').click(); await page.wait_for_function('!states[0].playing')
  check('html_preview_response_detected','did not return a WAV file' in (await state(page))['status'],await state(page))
  await browser.close()
try:
 asyncio.run(main())
finally:
 server.shutdown(); (ROOT/'player-browser-check.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps({'passed':sum(c['pass'] for c in report['checks']),'total':len(report['checks']),'report':str(ROOT/'player-browser-check.json')}))
