async page => {
 const checks=[],ensure=(ok,label)=>{if(!ok)throw Error(label);checks.push(label);};
 const address=new URL(page.url());address.hash='';address.search='';
 const errors=[],missing=[];
 page.on('pageerror',error=>errors.push(error.message));
 page.on('response',response=>{if(response.status()>=400&&response.url().startsWith(address.origin)&&!response.url().endsWith('/favicon.ico'))missing.push(response.url());});
 await page.unroute('**/assets/*.json');await page.unroute('**/ui/preview-data.js');
 await page.goto(address.href);
 await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 const embedded=await page.evaluate(()=>({product:window.MHE_PRODUCT_CATALOG,audio:window.MHE_AUDIO_CATALOG,preview:window.LONGSWORD_PREVIEW}));
 ensure(embedded.product?.version==='1.0.0-offline-rc1','actual ZIP embeds final release catalog');
 ensure(embedded.product.modes.length===6&&embedded.product.characters.length===33&&embedded.product.weapons.length===82,'actual ZIP embeds 6 modes / 33 characters / 82 models');
 ensure(embedded.preview?.clips.length===embedded.product.preview.clips&&embedded.preview.missing_source_previews.length===0,'actual ZIP embeds all evaluated source clips');
 ensure(embedded.product.characters.every(character=>Object.values(character.modes).every(mode=>mode.runtime_status==='pending')),'package preserves deferred game validation');
 const clips=Object.values(embedded.audio.modes).flatMap(mode=>mode.clips);
 ensure(clips.length===196&&clips.filter(clip=>clip.theme).length===8,'actual ZIP embeds all mode and special model PCM records');
 const dependencies=[embedded.product.preview.file,...embedded.product.source_tours.modes.map(tour=>tour.file),
  ...embedded.product.characters.flatMap(character=>Object.values(character.modes).flatMap(mode=>(mode.reference_images||[]).map(image=>image.file))),
  ...clips.map(clip=>clip.file)];
 const absent=await page.evaluate(async paths=>{const absent=[];for(let begin=0;begin<paths.length;begin+=10){await Promise.all(paths.slice(begin,begin+10).map(async path=>{const response=await fetch('../assets/'+path,{method:'HEAD'});if(!response.ok||Number(response.headers.get('content-length'))<=0)absent.push(path);}));}return absent;},dependencies);
 ensure(absent.length===0,'actual ZIP has full embedded metadata dependency closure');
 const pcm=await page.evaluate(async clips=>{const issues=[];for(const clip of clips){try{const response=await fetch('../assets/'+clip.file);if(!response.ok)throw Error('resource missing');const result=MHEWorkshop.pcmWav(await response.arrayBuffer());if(result.frames<1||result.channels!==2||result.sampleRate!==48000)throw Error('invalid PCM');}catch(error){issues.push(clip.id+': '+error.message);}}return issues;},clips);
 ensure(pcm.length===0,'all 196 packaged WAVs decode through the unified web PCM implementation');
 await page.route('**/assets/*.json',route=>route.abort());await page.reload();
 await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 ensure(await page.locator('#character-select option').count()===33,'actual ZIP works with embedded fallback and JSON reads unavailable');
 await page.locator('[data-page="loadout"]').click();
 for(const id of embedded.product.modes.map(mode=>mode.id)){
  await page.locator(`[data-mode="${id}"]`).click();await page.locator('[data-page="motion"]').click();
  ensure(await page.locator('#preview-clip option').count()>0,id+': packaged source controls populated');
  const video=page.locator('#source-tour-video');
  const tour=embedded.product.source_tours.modes.find(tour=>(tour.id||tour.mode)===id);
  ensure(await video.getAttribute('src')==='../assets/'+tour.file,id+': packaged source video uses its actual catalog resource');
  ensure(await video.getAttribute('preload')==='none'&&await video.evaluate(element=>element.paused&&!element.autoplay),id+': packaged source reference does not autoplay/preload');
  await page.locator('#play-source-tour').click();
  await page.waitForFunction(()=>{const video=document.getElementById('source-tour-video');return video.readyState>=2&&!video.paused&&video.currentTime>0;});
  ensure(await video.evaluate(element=>Number.isFinite(element.duration)&&element.duration>5),id+': actual packaged source MP4 decodes after explicit playback');
  await page.locator('#stop-source-tour').click();
  ensure(await video.evaluate(element=>element.paused&&element.currentTime===0),id+': packaged source reference stops and resets');
  const mode=embedded.product.modes.find(mode=>mode.id===id);
  ensure(mode.default_bindings.verified===true&&mode.default_bindings.verification_scope.includes('Physical'),id+': packaged certification remains physical-key-only');
  for(const preset of ['mouse5','mouse3']){
   await page.locator('#keyboard-preset').selectOption(preset);
   const keys=mode.default_bindings.presets[preset].keys;
   for(const key of mode.input_keys){ensure(Number(await page.locator('#key-'+key).inputValue())===keys[key],id+'/'+preset+'/'+key+': actual packaged physical preset applies');}
  }
  await page.locator('#show-target-preview').click();await page.waitForFunction(()=>{const image=document.getElementById('target-reference-image');return image.complete&&image.naturalWidth>0;});
  ensure(await page.locator('#target-reference').isVisible(),id+': packaged actual target mesh reference loads');
  await page.locator('[data-page="loadout"]').click();
 }
 ensure(await page.locator('#demo').isDisabled(),'packaged resources do not fabricate runtime readiness');
 ensure(errors.length===0&&missing.length===0,'actual package page has no script errors or missing resources');
 await page.unroute('**/assets/*.json');
 return{passed:checks.length,pcm_clips:clips.length,dependencies:dependencies.length,source_clips:embedded.preview.clips.length,source_videos:embedded.product.source_tours.modes.length,keyboard_presets:2,scope:'Actual extracted final ZIP served read-only. Embedded fallback, all PCM decoding, all source MP4 playback, physical presets and actual target/source dependency use; no game/server.'};
}
