async page => {
 const checks=[],ensure=(ok,label)=>{if(!ok)throw Error(label);checks.push(label);};
 const address=new URL(page.url());address.hash='';address.search='';
 const errors=[];page.on('pageerror',error=>errors.push(error.message));
 await page.goto(address.href);await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 const modes=await page.evaluate(async()=>(await(await fetch('../assets/product_catalog.json')).json()).modes);
 for(const mode of modes){
  await page.locator('[data-page="loadout"]').click();await page.locator(`[data-mode="${mode.id}"]`).click();await page.locator('[data-page="motion"]').click();
  ensure(mode.default_bindings.verified===true&&mode.default_bindings.verification_scope.includes('Physical'),mode.id+': certification is physical-key-only');
  ensure(mode.default_bindings.source_url==='https://game.capcom.com/manual/MHW_PC/en/steam/page/2/2',mode.id+': primary source attached');
  ensure(!['bow','light_bowgun'].includes(mode.id)||!mode.input_keys.includes('spirit'),mode.id+': inactive ranged SpiritBit is excluded');
  for(const preset of ['mouse5','mouse3']){
   await page.locator('#keyboard-preset').selectOption(preset);
   const keys=mode.default_bindings.presets[preset].keys;
   for(const key of mode.input_keys){ensure(Number(await page.locator('#key-'+key).inputValue())===keys[key],mode.id+'/'+preset+'/'+key+': actual control applies');}
   ensure(!(await page.locator('#key-conflicts').innerText()).includes('同为'),mode.id+'/'+preset+': no duplicate active physical key');
   await page.locator('#save-config').click();
   const saved=await page.evaluate(()=>JSON.parse(localStorage.getItem('mhe.workshop.draft.v2'))?.config);
   ensure(saved?.keyboard_preset===preset&&mode.input_keys.every(key=>saved.keys[key]===keys[key]),mode.id+'/'+preset+': saved contract matches physical controls');
  }
  await page.locator('#key-attack').selectOption('70');
  ensure(await page.locator('#keyboard-preset').inputValue()==='custom',mode.id+': manual edit becomes custom');
  await page.locator('#save-config').click();
 }
 await page.locator('[data-page="loadout"]').click();await page.locator('[data-mode="longsword"]').click();await page.locator('[data-page="motion"]').click();
 ensure(Number(await page.locator('#key-attack').inputValue())===70&&await page.locator('#keyboard-preset').inputValue()==='custom','mode switch preserves previous custom binding');
 const legacy=await page.evaluate(()=>MHEWorkshop.configuration({schema_version:1,mode:'bow',keys:{attack:1,secondary:5,spirit:17,dodge:32,sheathe:69,aim:2,reload:82},mode_keys:{lance:{attack:80}}}));
 ensure(legacy.keyboard_preset==='custom'&&legacy.keys.secondary===5&&legacy.keys.spirit===17&&legacy.mode_keys.lance.attack===80,'legacy bindings and per-mode overrides remain unchanged');
 ensure(errors.length===0,'no browser errors in all physical presets and custom migration');
 return{passed:checks.length,modes:modes.length,presets:2,scope:'Actual published mouse5/mouse3 metadata and real browser controls/save/custom migration. Weapon-specific combo certification remains separate.'};
}
