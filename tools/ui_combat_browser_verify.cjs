async page=>{
 const checks=[],ensure=(condition,label)=>{if(!condition)throw Error(label);checks.push(label);};
 const address=new URL(page.url());address.search='';address.hash='';
 const errors=[],missing=[];page.on('pageerror',error=>errors.push(error.message));
 page.on('response',response=>{if(response.status()>=400&&response.url().startsWith(address.origin)&&!response.url().endsWith('/favicon.ico'))missing.push(response.url());});
 await page.goto(address.href);await page.evaluate(()=>{localStorage.removeItem('mhe.workshop.draft.v2');localStorage.removeItem('mhe.workshop.presets.v2');});await page.reload();
 await page.waitForFunction(()=>document.getElementById('diagnostic-catalog').textContent.includes('资源目录已载入'));
 await page.locator('[data-page="practice"]').click();
 ensure(await page.locator('#target-select').isDisabled()&&await page.locator('#practice-attack').isDisabled(),'offline actions stay disabled');
 ensure(await page.locator('#control-lock_key').inputValue()==='81'&&await page.locator('#control-focus_key').inputValue()==='17'&&await page.locator('#control-cycle_key').inputValue()==='9','official Q/Ctrl plus module Tab defaults');
 ensure(await page.evaluate(()=>getComputedStyle(document.body).backgroundColor!=='rgba(0, 0, 0, 0)')&&(await page.locator('#diagnostic-catalog').innerText()).includes('33 角色'),'real stylesheet and local asset catalog loaded');
 await page.locator('button[aria-label="捕获选择目标快捷键"]').click();await page.keyboard.press('u');
 ensure(await page.locator('#control-lock_key').inputValue()==='85','control key capture applies independently');
 await page.locator('button[aria-label="捕获镜头朝向目标快捷键"]').click();await page.keyboard.press('Escape');
 ensure(!await page.locator('#capture-dialog').isVisible()&&await page.locator('#control-focus_key').inputValue()==='17','Esc cancels capture without changing focus key');
 await page.locator('button[aria-label="捕获选择目标快捷键"]').click();await page.keyboard.press('F8');
 ensure(await page.locator('#control-lock_key').inputValue()==='85'&&(await page.locator('#notice').innerText()).includes('保留'),'reserved F8 capture is rejected with original value intact');
 await page.locator('#control-focus_key').selectOption('18');await page.locator('#control-cycle_key').selectOption('88');
 await page.locator('[data-field="target_camera"]').selectOption('target');await page.locator('[data-field="target_range"]').fill('42.5');
 await page.locator('#save-config').click();
 const draft=await page.evaluate(()=>JSON.parse(localStorage.getItem('mhe.workshop.draft.v2')).config);
 ensure(draft.lock_key===85&&draft.focus_key===18&&draft.cycle_key===88&&draft.target_camera==='target'&&draft.target_range===42.5,'all camera fields saved in offline draft');
 for(const mode of ['longsword','sword_shield','greatsword','lance','light_bowgun','bow']){
  await page.locator('[data-page="loadout"]').click();await page.locator(`[data-mode="${mode}"]`).click();await page.locator('[data-page="motion"]').click();await page.locator('#keyboard-preset').selectOption('mouse3');await page.locator('[data-page="practice"]').click();
  ensure(await page.locator('#control-lock_key').inputValue()==='85'&&await page.locator('#control-focus_key').inputValue()==='18'&&await page.locator('#control-cycle_key').inputValue()==='88',mode+': action preset preserves camera controls');
 }
 await page.locator('#control-focus_key').selectOption('85');
 ensure((await page.locator('#control-key-conflicts').innerText()).includes('同为'),'duplicate camera keys show a hard conflict');
 await page.locator('#save-config').click();ensure((await page.locator('#save-state').innerText()).includes('保存失败'),'duplicate controls cannot be persisted');
 await page.locator('#control-focus_key').selectOption('18');await page.locator('#save-config').click();
 await page.locator('[data-page="presets"]').click();await page.locator('#preset-name').fill('战斗控制往返');
 const downloadWait=page.waitForEvent('download');await page.locator('#preset-export').click();const download=await downloadWait;await download.saveAs('output/playwright/combat-control-preset.json');
 await page.locator('#preset-file').setInputFiles('output/playwright/combat-control-preset.json');await page.locator('#preset-import').click();
 ensure((await page.locator('#preset-result').innerText()).includes('已导入'),'exported controls can be imported');
 await page.locator('#preset-list .preset-row').filter({hasText:'战斗控制往返'}).locator('button').filter({hasText:'载入'}).click();
 await page.locator('[data-page="practice"]').click();ensure(await page.locator('#control-lock_key').inputValue()==='85'&&await page.locator('[data-field="target_range"]').inputValue()==='42.5','preset import/load keeps camera values');
 await page.addInitScript(()=>{
  if(!location.search.includes('combat_test=1'))return;
  let config={mode:'longsword',local_practice:true,practice_target:'dummy',target_lock:true,target_camera:'focus',combat_rules:true},revision=1,callback;
  const test=window.MHE_COMBAT_TEST={connected:true,active:true,auto:false,selected:0,generation:3,requests:[],lastIncoming:null};
  const state=()=>({runtime_ready:true,active:test.active,auto_demo:test.auto,mode:config.mode,character:'chr_0016_laevat',config_revision:revision,applied_config_revision:revision,capabilities:{modes:['longsword','sword_shield','greatsword','lance','light_bowgun','bow'],pause:true,hud_edit:true},
   targeting:{enabled:config.target_lock,mode:config.target_camera,selected_id:test.selected,selected_generation:test.generation,approximate_geometry:true,detail:'只读模型包络 · 浏览器契约桩'},
   combat:{enabled:config.local_practice&&config.combat_rules&&!test.auto,numeric_scope:'仅本地模拟',followup_pending:false},
   practice:{enabled:config.local_practice,hits:0,total:0,targets:[{id:101,name:'测试靶 A',hp:1000,max_hp:1000},{id:202,name:'测试靶 B',hp:1000,max_hp:1000}]}});
  window.betterEndfield={status:async()=>({connected:test.connected}),readConfig:async()=>structuredClone(config),saveConfig:async value=>{config=structuredClone(value);revision++;setTimeout(()=>callback?.({kind:'event',body:state()}),5);},onmessage:fn=>callback=fn,
   send:async body=>{test.requests.push(structuredClone(body));if(body.action==='target_select')test.selected=101;if(body.action==='target_cycle'){test.selected=202;test.generation=5;}if(body.action==='target_release')test.selected=0;if(body.action==='practice_attack')test.lastIncoming=structuredClone(body);return{kind:'reply',result:0,body:{...state(),request_id:body.request_id,action:body.action}};}};
 });
 await page.setViewportSize({width:1365,height:900});await page.goto(address.href+'?combat_test=1#practice');
 await page.waitForFunction(()=>document.getElementById('connection').textContent==='EML 已连接');
 ensure(!await page.locator('#target-select').isDisabled()&&await page.locator('#target-focus').isDisabled(),'reported readiness enables selection, not unselected focus');
 await page.locator('#target-select').click();ensure((await page.locator('#target-selected').innerText()).includes('测试靶 A')&&!await page.locator('#target-focus').isDisabled(),'selection uses host identity and enables camera actions');
 await page.locator('#target-focus').click();await page.locator('#target-cycle').click();
 ensure((await page.locator('#target-selected').innerText()).includes('测试靶 B')&&(await page.locator('#target-selected').innerText()).includes('代次 5'),'cycle reports stable identity generation');
 await page.locator('#target-release').click();ensure((await page.locator('#target-selected').innerText()).includes('暂无')&&await page.locator('#target-focus').isDisabled(),'release clears selected status');
 ensure((await page.locator('#target-geometry-detail').innerText()).includes('近似')&&(await page.locator('#combat-rule-state').innerText()).includes('本地'),'source windows and approximate target geometry retain distinct evidence');
 await page.locator('#practice-attack').click();ensure((await page.evaluate(()=>MHE_COMBAT_TEST.lastIncoming)).from_front===true,'front practice attack follows explicit transport field');
 await page.locator('#practice-attack-direction').selectOption('back');await page.locator('#practice-attack').click();
 ensure((await page.evaluate(()=>MHE_COMBAT_TEST.lastIncoming)).from_front===false,'rear practice attack keeps directional condition');
 await page.evaluate(()=>MHE_COMBAT_TEST.auto=true);await page.locator('#refresh').click();ensure(await page.locator('#practice-attack').isDisabled(),'automatic showcase never generates practice incoming');
 await page.evaluate(()=>MHE_COMBAT_TEST.auto=false);await page.locator('#refresh').click();
 await page.locator('[data-field="practice_target"]').selectOption('monsters');await page.locator('#save-config').click();
 ensure(await page.locator('#practice-attack').isDisabled()&&(await page.locator('#practice-attack-detail').innerText()).includes('本地练习靶'),'read-only monsters cannot receive synthetic incoming requests');
 await page.locator('[data-field="practice_target"]').selectOption('dummy');await page.locator('[data-field="target_camera"]').selectOption('target');await page.locator('#save-config').click();
 ensure((await page.locator('#target-lock-state').innerText()).includes('持续跟随'),'saved target camera mode is rendered from host state');
 await page.locator('#control-focus_key').selectOption('32');
 ensure((await page.locator('#control-key-conflicts').innerText()).includes('动作键优先'),'action/control overlap warns with native priority');
 await page.locator('#save-config').click();ensure((await page.locator('#save-state').innerText()).includes('已保存'),'legacy action/control overlap remains saveable');
 const requestCount=await page.evaluate(()=>MHE_COMBAT_TEST.requests.filter(x=>x.action!=='status').length);
 await page.locator('button[aria-label="捕获选择目标快捷键"]').click();await page.keyboard.press('Escape');
 ensure(await page.evaluate(()=>MHE_COMBAT_TEST.requests.filter(x=>x.action!=='status').length)===requestCount,'web capture cancellation sends no gameplay action');
 await page.locator('#control-focus_key').selectOption('18');await page.locator('#save-config').click();await page.locator('#target-select').click();
 await page.screenshot({path:'output/playwright/combat-practice-desktop.png',fullPage:true});
 for(const width of [390,768,1365]){await page.setViewportSize({width,height:900});ensure(!await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),width+'px combat controls have no document overflow');}
 await page.setViewportSize({width:390,height:844});await page.screenshot({path:'output/playwright/combat-practice-mobile.png',fullPage:true});
 await page.evaluate(()=>MHE_COMBAT_TEST.connected=false);await page.locator('#refresh').click();
 ensure(await page.locator('#target-select').isDisabled()&&await page.locator('#target-cycle').isDisabled()&&await page.locator('#practice-attack').isDisabled()&&(await page.locator('#target-selected').innerText()).includes('暂无'),'disconnect invalidates live target controls and status');
 ensure(errors.length===0,'no pageerror in control editing or host contract');ensure(missing.length===0,'local styles, assets and preview requests have no missing responses');
 return {passed:checks.length,errors,missing,screenshots:['output/playwright/combat-practice-desktop.png','output/playwright/combat-practice-mobile.png'],scope:'Real Edge with actual source UI/assets and explicitly injected isolated host-contract stub; no game/server/runtime camera verification.'};
}
