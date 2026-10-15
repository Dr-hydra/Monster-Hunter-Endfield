async (page) => {
 const results=[],ensure=(ok,label)=>{if(!ok)throw Error(label);results.push(label);};
 const pageErrors=[],failedResources=[];
 page.on('pageerror',error=>pageErrors.push(error.message));
 page.on('response',response=>{if(response.status()>=400&&response.url().includes('127.0.0.1:8877')&&!response.url().endsWith('/favicon.ico'))failedResources.push(`${response.status()} ${response.url()}`);});
 await page.goto('http://127.0.0.1:8877/ui/index.html');
 await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 const data=await page.evaluate(async()=>({product:await(await fetch('../assets/product_catalog.json')).json(),audio:await(await fetch('../assets/audio_catalog.json')).json(),preview:await(await fetch('../assets/ui_modes_preview.json')).json()}));
 const roster=await page.evaluate(async()=>({characters:(await(await fetch('../profiles/index.json')).json()).characters.map(c=>c.character_id),weapons:(await(await fetch('../profiles/weapons/index.json')).json()).weapons.map(w=>w.id)}));
 ensure(data.product.modes.length===6&&JSON.stringify(data.product.characters.map(c=>c.id).sort())===JSON.stringify(roster.characters.sort()),'actual six modes / current character profiles');
 ensure(JSON.stringify(data.product.weapons.map(w=>w.id).sort())===JSON.stringify(roster.weapons.sort()),'actual current equipment and special profiles');
 ensure(data.preview.missing_source_previews.length===0,'all core source previews available');
 ensure(data.product.characters.every(c=>Object.values(c.modes).every(m=>m.runtime_status==='pending')),'no fabricated gameplay verification');
 const dependencies=[data.product.preview.file,...(data.product.source_tours?.modes||[]).map(m=>m.file),
  ...data.product.characters.flatMap(c=>Object.values(c.modes).flatMap(m=>(m.reference_images||[]).map(i=>i.file))),
  ...Object.values(data.audio.modes).flatMap(m=>m.clips.map(c=>c.file))];
 const missing=await page.evaluate(async(paths)=>{const missing=[];for(let i=0;i<paths.length;i+=8){await Promise.all(paths.slice(i,i+8).map(async file=>{const r=await fetch('../assets/'+file,{method:'HEAD'});if(!r.ok||Number(r.headers.get('content-length'))<=0)missing.push(file);}));}return missing;},dependencies);
 ensure(missing.length===0,'complete sound / source-preview / target-image browser dependency closure');
 await page.locator('[data-page="loadout"]').click();ensure(await page.locator('#weapon-record option').count()===roster.weapons.length,'complete weapon metadata browsing');
 for(const mode of data.product.modes){
  await page.locator(`[data-mode="${mode.id}"]`).click();await page.locator('[data-page="motion"]').click();
  ensure(await page.locator('#move-list .move').count()===mode.moves.length,`${mode.id}: actual command catalog`);
  await page.waitForFunction(()=>!document.getElementById('preview-clip').disabled);
  ensure(await page.locator('#preview-clip option').count()>0,`${mode.id}: actual evaluated source preview`);
  await page.locator('#pause-preview').click();await page.locator('#step-preview').click();await page.waitForTimeout(50);
  ensure((await page.locator('#preview-frame').innerText()).includes('/'),`${mode.id}: frame controls work`);
  ensure(await page.locator('#key-list .key-row').count()===mode.input_keys.length,`${mode.id}: actual input semantics`);
  await page.locator('#keyboard-preset').selectOption('mouse5');await page.locator('#reset-keys').click();
  for(const key of mode.input_keys){ensure(Number(await page.locator('#key-'+key).inputValue())===mode.default_bindings.keys[key],`${mode.id}/${key}: coherent module defaults apply regardless of official-proof flag`);}
  await page.locator('[data-page="presentation"]').click();ensure(await page.locator('#audio-list .audio-item').count()===data.audio.modes[mode.id].clips.length,`${mode.id}: all source sound entries`);
  await page.locator('#audio-list button').first().click();await page.waitForTimeout(150);
  ensure(!(await page.locator('#audio-preview-state').innerText()).includes('失败'),`${mode.id}: PCM decode / balanced audition`);
  await page.locator('#stop-audition').click();await page.locator('[data-page="loadout"]').click();
 }
 await page.locator('#character-select').selectOption('chr_0003_endminf');await page.locator('[data-mode="bow"]').click();await page.locator('[data-page="motion"]').click();
 ensure(!await page.locator('#show-target-preview').isDisabled(),'real offline mesh reference entry');
 await page.locator('#show-target-preview').click();ensure(await page.locator('#target-reference').isVisible(),'target mesh reference displayed');
 await page.waitForFunction(()=>document.getElementById('target-reference-image').naturalWidth>0);
 ensure((await page.locator('#target-reference-caption').innerText()).includes('不随源动作时间轴同步'),'static mesh reference accurately scoped');
 await page.screenshot({path:'output/playwright/workshop-actual-mesh.png',fullPage:true});
 await page.locator('[data-page="loadout"]').click();await page.screenshot({path:'output/playwright/workshop-actual-catalog.png',fullPage:true});
 // Every published compatibility cell must have a discoverable, loadable image.
 let checkedCells=0;
 for(const character of data.product.characters){
  await page.locator('#character-select').selectOption(character.id);
  for(const mode of data.product.modes){await page.locator(`[data-mode="${mode.id}"]`).click();await page.locator('[data-page="motion"]').click();
   ensure(!await page.locator('#show-target-preview').isDisabled(),`${character.id}/${mode.id}: reference discoverable`);
   await page.locator('#show-target-preview').click();await page.waitForFunction(()=>{const image=document.getElementById('target-reference-image');return image.complete&&image.naturalWidth>0;});checkedCells++;
   await page.locator('[data-page="loadout"]').click();
  }
 }
 ensure(checkedCells===roster.characters.length*data.product.modes.length,'all current actual mesh references loaded through UI');
 await page.locator('#character-select').selectOption('chr_0034_typhoea');await page.locator('[data-mode="bow"]').click();await page.locator('[data-page="motion"]').click();await page.locator('#show-target-preview').click();
 ensure(await page.locator('#target-view option').count()>=3,'priority bow front / side / default mesh views');await page.locator('#target-view').selectOption({label:'侧面'});await page.waitForFunction(()=>document.getElementById('target-reference-image').naturalWidth>0);await page.screenshot({path:'output/playwright/workshop-priority-bow-side.png',fullPage:true});
 ensure(pageErrors.length===0,'no page JavaScript errors');ensure(failedResources.length===0,'no missing browser resources');
 return{passed:results.length,results,source_clips:data.preview.clips.length,audio_clips:Object.values(data.audio.modes).reduce((n,x)=>n+x.clips.length,0),mesh_checked:data.product.characters.flatMap(c=>Object.values(c.modes)).filter(m=>m.offline_status==='mesh_checked'||m.offline_status==='passed').length,scope:'Actual local product metadata / evaluated source animation / exported target mesh references / source PCM in real Edge. No game or live EML host launched.'};
}
