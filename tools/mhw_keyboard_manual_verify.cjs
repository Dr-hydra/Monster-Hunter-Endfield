async page => {
 const base='https://game.capcom.com/manual/MHW_PC/';
 await page.goto(base);await page.locator('label[for="en"]').click();
 await page.locator('form[lang="en"] select[name="region"]').selectOption('gb');
 await page.waitForURL('**/en/steam');
 await page.goto(base+'en/steam/page/2/2');
 await page.locator('main').waitFor();
 const evidence=await page.evaluate(()=>({url:location.href,title:document.title,
  text:document.querySelector('main').innerText,
  icons:[...document.querySelectorAll('main img')].map(image=>({src:image.getAttribute('src'),alt:image.alt}))}));
 if(!evidence.text.includes('This page lists the default controls for the keyboard.'))throw Error('Not the official keyboard page');
 await page.screenshot({path:'output/playwright/mhw-official-pc-keyboard.png',fullPage:true});
 return evidence;
}
