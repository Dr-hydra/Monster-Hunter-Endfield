async page => {
 const checks=[],ensure=(ok,label)=>{if(!ok)throw Error(label);checks.push(label);};
 const address=new URL(page.url());address.hash='';address.search='';
 const errors=[];page.on('pageerror',error=>errors.push(error.message));
 await page.goto(address.href);
 await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 const catalogs=await page.evaluate(async()=>({product:await(await fetch('../assets/product_catalog.json')).json(),audio:await(await fetch('../assets/audio_catalog.json')).json()}));
 ensure(catalogs.product.version==='1.0.0-offline-rc1','release version in actual product catalog');
 await page.locator('[data-page="start"]').click();
 for(const value of ['full','basic','signature']){await page.locator('#demo-sequence').selectOption(value);ensure(await page.locator('#demo-sequence').inputValue()===value,'offline sequence choice: '+value);}
 await page.locator('#demo-loop').uncheck();await page.locator('#save-config').click();
 const saved=await page.evaluate(()=>JSON.parse(localStorage.getItem('mhe.workshop.draft.v2'))?.config);
 ensure(saved?.demo_sequence==='signature'&&saved.demo_loop===false,'offline draft preserves sequence and single-run policy');
 ensure(await page.locator('#demo').isDisabled(),'offline edited sequence does not enable runtime playback');
 await page.locator('[data-page="practice"]').click();
 await page.locator('[data-field="projectile_speed"]').fill('42');await page.locator('#save-config').click();
 ensure(await page.evaluate(()=>JSON.parse(localStorage.getItem('mhe.workshop.draft.v2'))?.config.projectile_speed===42),'local projectile speed control saved');
 let themes=0;
 for(const id of ['longsword','bow']){
  await page.locator('[data-page="loadout"]').click();await page.locator(`[data-mode="${id}"]`).click();await page.locator('[data-page="presentation"]').click();
  if(!await page.locator('[data-field="sound_enabled"]').isChecked())await page.locator('[data-field="sound_enabled"]').check();
  const clips=catalogs.audio.modes[id].clips.filter(clip=>clip.theme);
  ensure(clips.length===(id==='longsword'?2:6),id+': actual special original PCM variants');
  for(const clip of clips){
   ensure(clip.source.startsWith('Endfield · ')&&clip.timing_evidence&&clip.status==='available',clip.id+': source and lifecycle evidence');
   ensure(clip.category===(id==='longsword'?'fire':clip.used_by.includes('原弓拉弦')?'charge':'weapon'),clip.id+': native volume category');
   const row=page.locator('#audio-list .audio-item').filter({has:page.locator('strong',{hasText:clip.label})});
   ensure(await row.count()===1,clip.id+': discoverable specific variant');await row.locator('button').click();
   await page.waitForFunction(label=>document.getElementById('audio-preview-state').textContent.includes('正在试听：'+label),clip.label);
   await page.locator('#stop-audition').click();ensure((await page.locator('#audio-preview-state').innerText()).includes('已停止'),clip.id+': explicit PCM stop');themes++;
  }
 }
 ensure(themes===8,'all eight extracted special model sounds auditioned');
 ensure(errors.length===0,'no page errors in release controls/special PCM');
 return{passed:checks.length,special_pcm:themes,scope:'Actual release catalogs, offline control persistence and extracted special PCM through web unified decode. No game/server.'};
}
