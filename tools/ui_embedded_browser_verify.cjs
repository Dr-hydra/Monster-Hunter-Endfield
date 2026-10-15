async (page) => {
 const ensure=(value,label)=>{if(!value)throw Error(label);};
 await page.unroute('**/assets/*.json');await page.unroute('**/ui/preview-data.js');
 await page.goto('http://127.0.0.1:8877/ui/index.html');
 const embedded=await page.evaluate(async()=>({
  product:await(await fetch('../assets/product_catalog.json')).json(),
  audio:await(await fetch('../assets/audio_catalog.json')).json(),
  preview:await(await fetch('../assets/ui_modes_preview.json')).json()}));
 await page.route('**/ui/preview-data.js',route=>route.fulfill({contentType:'application/javascript',body:
  'window.MHE_PRODUCT_CATALOG='+JSON.stringify(embedded.product)+';window.MHE_AUDIO_CATALOG='+JSON.stringify(embedded.audio)+';window.LONGSWORD_PREVIEW='+JSON.stringify(embedded.preview)+';'}));
 await page.route('**/assets/*.json',route=>route.abort());
 await page.reload();
 await page.waitForFunction(()=>document.getElementById('catalog-state').textContent==='资源目录已载入');
 ensure(await page.locator('#character-select option').count()===embedded.product.characters.length,'embedded actual character metadata');
 await page.locator('[data-page="motion"]').click();
 await page.waitForFunction(()=>!document.getElementById('preview-clip').disabled);
 ensure(await page.locator('#preview-clip option').count()>0,'embedded source curves available without JSON fetch');
 await page.locator('[data-page="loadout"]').click();
 ensure(await page.locator('#weapon-record option').count()===embedded.product.weapons.length,'embedded current model records available');
 ensure(await page.locator('#demo').isDisabled(),'embedded metadata never fabricates live host readiness');
 await page.unroute('**/assets/*.json');
 await page.unroute('**/ui/preview-data.js');
 return{passed:4,source_clips:embedded.preview.clips.length,scope:'ZIP-style embedded metadata fallback with JSON requests blocked; PCM still uses explicit HTTP/EML resources.'};
}
