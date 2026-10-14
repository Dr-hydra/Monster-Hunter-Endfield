async (page) => {
 const results=[],ensure=(ok,label)=>{if(!ok)throw Error(label);results.push(label);};
 const address='http://127.0.0.1:8877/ui/index.html';
 await page.goto(address);await page.evaluate(()=>{localStorage.removeItem('mhe.workshop.draft.v2');localStorage.removeItem('longsword.configuration');const k='mhe.workshop.presets.v2';localStorage.setItem(k,JSON.stringify(JSON.parse(localStorage.getItem(k)||'[]').filter(x=>!['浏览器验证 · 弓','旧版'].includes(x.name))));});await page.reload();await page.waitForTimeout(400);
 ensure(await page.locator('#demo').isDisabled(),'offline runtime controls disabled');
 await page.locator('[data-page="loadout"]').click();
 ensure(await page.locator('.mode-card').count()===6,'six mode cards');
 ensure(await page.locator('#character-select option').count()===33,'33 selectable character records');
 await page.locator('[data-mode="lance"]').click();ensure((await page.locator('#move-list').innerText()).includes('突刺'),'lance thrust catalog, no insect glaive');
 await page.locator('#save-config').click();ensure((await page.locator('#save-state').innerText()).includes('离线草稿'),'offline saving does not claim applied');
 await page.locator('[data-page="start"]').click();await page.locator('[data-recommend="typhoea"]').click();
 ensure(await page.locator('#character-select').inputValue()==='chr_0034_typhoea','bow priority recommendation');
 ensure(await page.locator('#visual-weapon').inputValue()==='typhoea_bow','bow independent visual selection');
 await page.locator('[data-page="presets"]').click();await page.locator('#preset-name').fill('浏览器验证 · 弓');await page.locator('#preset-save').click();
 ensure((await page.locator('#preset-list').innerText()).includes('浏览器验证 · 弓'),'preset local save');
 await page.locator('#preset-save').click();ensure(await page.locator('#confirm-dialog').isVisible(),'preset overwrite explicit dialog');await page.locator('#confirm-cancel').click();
 await page.locator('#preset-json').fill('{"version":99,"config":{"mode":"bow"}}');await page.locator('#preset-import').click();ensure((await page.locator('#preset-result').innerText()).includes('较新版本'),'future version rejected');
 await page.locator('#preset-json').fill('{"format":"mhe.preset","version":1,"name":"旧版","config":{"speed":0.75,"spirit_key":17}}');await page.locator('#preset-import').click();ensure((await page.locator('#preset-result').innerText()).includes('已导入'),'legacy config migrated import');
 await page.locator('[data-page="motion"]').click();await page.locator('button[aria-label="捕获攻击按键"]').click();await page.keyboard.press('Escape');ensure(!await page.locator('#capture-dialog').isVisible(),'Escape cancels key capture');
 await page.locator('#key-secondary').selectOption('1');ensure((await page.locator('#key-conflicts').innerText()).includes('同为'),'key conflicts explicit');
 await page.locator('#reset-keys').click();ensure(!(await page.locator('#key-conflicts').innerText()).includes('同为'),'mode reset resolves hard conflicts, contextual priority stays explicit');
 for(const width of [390,768,1365,1920]){
  await page.setViewportSize({width,height:1000});
  for(const tab of ['start','loadout','motion','presentation','practice','presets','help']){await page.locator(`[data-page="${tab}"]`).click();const overflow=await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1);ensure(!overflow,`${width}px ${tab}: no document overflow`);}
 }
 // Effective CSS viewport at a 200% browser-scale equivalent.
await page.setViewportSize({width:682,height:450});await page.locator('[data-page="presentation"]').click();ensure(!await page.evaluate(()=>document.documentElement.scrollWidth>innerWidth+1),'200% effective viewport no overflow');
await page.locator('[data-field="theme"]').selectOption('high_contrast');ensure(await page.evaluate(()=>document.documentElement.dataset.theme)==='high_contrast','high contrast theme');
const contrast=await page.evaluate(()=>{const rgb=s=>s.match(/\d+/g).slice(0,3).map(Number),l=v=>{const x=v/255;return x<=.04045?x/12.92:((x+.055)/1.055)**2.4},lum=c=>.2126*l(c[0])+.7152*l(c[1])+.0722*l(c[2]),rate=(a,b)=>{const x=lum(a),y=lum(b);return(Math.max(x,y)+.05)/(Math.min(x,y)+.05)};const style=getComputedStyle(document.documentElement),bg=rgb(getComputedStyle(document.body).backgroundColor),text=rgb(getComputedStyle(document.body).color),panel=rgb(getComputedStyle(document.querySelector('.panel')).backgroundColor),border=rgb(getComputedStyle(document.querySelector('.panel')).borderTopColor);return {text:rate(text,bg),control:rate(border,panel)};});ensure(contrast.text>=4.5&&contrast.control>=3,'high contrast text and controls pass contrast requirements');
 await page.setViewportSize({width:1365,height:1000});await page.locator('[data-page="start"]').click();await page.screenshot({path:'output/playwright/workshop-start.png',fullPage:true});
 await page.setViewportSize({width:390,height:844});await page.locator('[data-page="presentation"]').click();await page.screenshot({path:'output/playwright/workshop-mobile.png',fullPage:true});
 // Mock host transport exercises browser state semantics; it never runs game code.
 await page.addInitScript(()=>{if(!location.search.includes('mock=1'))return;
  let config={mode:'longsword'},callback,revision=1;window.MHE_TEST={fail:false,connected:true,sends:0};
  const state=()=>({runtime_ready:true,auto_demo:!!window.MHE_TEST.active,active:!!window.MHE_TEST.active,paused:!!window.MHE_TEST.paused,demo_progress:.25,demo_elapsed:12.5,demo_sequences:['full','basic','signature'],character:'chr_0016_laevat',mode:config.mode||'longsword',config_revision:revision,applied_config_revision:revision,capabilities:{modes:['longsword','bow','lance'],pause:true,hud_edit:true},motion_label:'气刃斩',version:'mock-contract-only',resource:{label:'模拟气刃',value:70,max:100,level:1,local_only:true,blocks:2,feedback:'模拟格挡成功'},projectiles:3,aim:{active:false,charge:0},practice:{enabled:true,hits:3,total:300,targets:[{id:1,name:'模拟目标',hp:700,max_hp:1000}]}});
  window.betterEndfield={status:async()=>({connected:window.MHE_TEST.connected}),readConfig:async()=>structuredClone(config),saveConfig:async value=>{if(window.MHE_TEST.fail)throw Error('模拟保存失败');config=structuredClone(value);revision++;setTimeout(()=>callback?.({kind:'event',body:state()}),5);},send:async body=>{window.MHE_TEST.sends++;if(body.action==='demo')window.MHE_TEST.active=true;if(body.action==='stop')window.MHE_TEST.active=false;if(body.action==='pause')window.MHE_TEST.paused=true;if(body.action==='resume')window.MHE_TEST.paused=false;setTimeout(()=>callback?.({kind:'reply',result:window.MHE_TEST.fail?2:0,body:{...state(),request_id:body.request_id,action:body.action,...(window.MHE_TEST.fail?{error:'模拟请求失败'}:{})}}),5);},onmessage:fn=>callback=fn};
 });
 await page.setViewportSize({width:1365,height:900});await page.goto(address+'?mock=1');await page.waitForTimeout(500);
 ensure(!await page.locator('#demo').isDisabled(),'mock host readiness enables supported mode');
 ensure((await page.locator('#resource-state').innerText()).includes('70 / 100'),'local resource value reflects reported state');ensure(Number(await page.locator('#resource-progress').getAttribute('max'))===100,'local resource progress follows actual maximum');ensure((await page.locator('#resource-detail').innerText()).includes('本地格挡 2 次'),'local guard feedback uses reported count');
 await page.locator('#demo-sequence').selectOption('signature');await page.locator('#demo-loop').uncheck();await page.locator('#save-config').click();await page.waitForTimeout(120);ensure(await page.locator('#demo-sequence').inputValue()==='signature'&&!await page.locator('#demo-loop').isChecked(),'sequence and single-run selection persist through host save');
 await page.locator('#demo').click();await page.waitForTimeout(100);ensure((await page.locator('#active-state').innerText()).includes('自动演示中'),'demo execution reflected from host state');
 ensure(Number(await page.locator('#demo-progress').getAttribute('value'))===.25&&(await page.locator('#demo-progress-label').innerText()).includes('12.5'),'demo progress and elapsed follow native status');
 await page.locator('#pause-runtime').click();await page.waitForTimeout(100);ensure((await page.locator('#active-state').innerText()).includes('暂停'),'pause reflected from host state');
 await page.locator('#stop').click();await page.waitForTimeout(100);ensure((await page.locator('#active-state').innerText()).includes('已结束'),'stop reflected from host state');
 await page.locator('[data-page="presentation"]').click();await page.locator('[data-field="sound_volume"]').fill('0.4');await page.locator('#save-config').click();await page.waitForTimeout(200);
 ensure((await page.locator('#save-state').innerText()).includes('宿主已应用'),'save and applied revisions distinguished');
 await page.locator('#audio-list button').first().click();await page.waitForTimeout(200);ensure(! (await page.locator('#audio-preview-state').innerText()).includes('失败'),'actual WAV PCM audition output');await page.locator('#stop-audition').click();ensure((await page.locator('#audio-preview-state').innerText()).includes('已停止'),'audition stop');
 await page.evaluate(()=>window.MHE_TEST.fail=true);await page.locator('[data-field="sound_volume"]').fill('0.35');await page.locator('#save-config').click();ensure((await page.locator('#save-state').innerText()).includes('保存失败'),'mock transport save failure preserves editor');
 await page.evaluate(()=>window.MHE_TEST.connected=false);await page.locator('#refresh').click();ensure(await page.locator('#demo').isDisabled(),'disconnect disables runtime controls');ensure((await page.locator('#resource-state').innerText()).includes('等待'),'disconnect invalidates live resource display');
 ensure(Number(await page.locator('#demo-progress').getAttribute('value'))===0,'disconnect invalidates demo progress');
 await page.locator('[data-page="practice"]').click();await page.screenshot({path:'output/playwright/workshop-practice.png',fullPage:true});
 await page.evaluate(report=>window.MHE_BROWSER_REPORT=report,{passed:results.length,results});return {passed:results.length,results,scope:'Real Edge browser, isolated mock transport and local PCM. No game or real EML session was launched.'};
}
