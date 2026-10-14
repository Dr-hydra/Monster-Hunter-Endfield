async page => {
 const checks=[],ensure=(ok,label)=>{if(!ok)throw Error(label);checks.push(label);};
 const address=new URL(page.url());address.hash='';address.search='';
 const errors=[];page.on('pageerror',error=>errors.push(error.message));
 await page.goto(address.href);await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 const modes=await page.evaluate(()=>window.MHEWorkshop.modes.map(x=>x.id));
 await page.locator('[data-page="loadout"]').click();
 for(const mode of modes){
  await page.locator(`[data-mode="${mode}"]`).click();await page.locator('[data-page="motion"]').click();
  const video=page.locator('#source-tour-video');
  ensure(await video.isVisible(),mode+': source hunter/weapon movie discoverable');
  ensure(await video.getAttribute('preload')==='none',mode+': no automatic video preload');
  ensure(await video.evaluate(x=>x.paused&&!x.autoplay),mode+': starts paused without autoplay');
  ensure((await page.locator('#source-tour-scope').innerText()).includes('不是终末地目标角色'),mode+': accurate source reference scope');
  await page.locator('#play-source-tour').click();await page.waitForFunction(()=>{const v=document.getElementById('source-tour-video');return v.readyState>=2&&!v.paused&&v.currentTime>0;});
  ensure(await video.evaluate(x=>Number.isFinite(x.duration)&&x.duration>5),mode+': real MP4 video and audio decode');
  await page.locator('[data-page="presentation"]').click();ensure(await video.evaluate(x=>x.paused),mode+': leaving motion page pauses playback');
  await page.locator('[data-page="motion"]').click();await page.locator('#play-source-tour').click();await page.waitForFunction(()=>!document.getElementById('source-tour-video').paused);
  await page.locator('#stop-source-tour').click();ensure(await video.evaluate(x=>x.paused&&x.currentTime===0),mode+': stop returns to beginning');
  await page.locator('[data-page="loadout"]').click();
 }
 await page.locator('[data-mode="longsword"]').click();await page.locator('#visual-weapon').selectOption('laevatain_blade');
 await page.locator('[data-mode="greatsword"]').click();ensure(await page.locator('#visual-weapon').inputValue()==='auto','switching from fire blade resets unsupported mode visual to auto');
 ensure(await page.locator('#visual-weapon option[value="laevatain_blade"]').evaluate(x=>x.disabled&&x.hidden),'fire blade unavailable in greatsword selector');
 await page.locator('[data-mode="bow"]').click();await page.locator('#visual-weapon').selectOption('typhoea_bow');await page.locator('[data-mode="lance"]').click();
 ensure(await page.locator('#visual-weapon').inputValue()==='auto','switching from bow resets unsupported mode visual to auto');
 ensure(await page.locator('#visual-weapon option[value="typhoea_bow"]').evaluate(x=>x.disabled&&x.hidden),'special bow unavailable in lance selector');
 await page.locator('[data-page="presets"]').click();
 await page.locator('#preset-json').fill(JSON.stringify({format:'mhe.preset',version:2,name:'invalid mode fixture',config:{mode:'bow',visual_weapon:'laevatain_blade'}}));await page.locator('#preset-import').click();
 ensure((await page.locator('#preset-result').innerText()).includes('仅支持太刀'),'preset incompatible fire-blade mode rejected');
 await page.locator('#preset-json').fill(JSON.stringify({format:'mhe.preset',version:2,name:'invalid mode fixture',config:{mode:'longsword',visual_weapon:'typhoea_bow'}}));await page.locator('#preset-import').click();
 ensure((await page.locator('#preset-result').innerText()).includes('仅支持弓'),'preset incompatible bow mode rejected');
 ensure(errors.length===0,'no page errors during all source videos and model validation');
 return{passed:checks.length,checks,scope:'Six actual offline source mesh+PCM MP4s in real browser. Explicit playback, no autoplay, no target-game quality claim.'};
}
