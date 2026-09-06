const { chromium } = require('C:/Users/zhang/.cache/codex-runtimes/codex-primary-runtime/dependencies/node/node_modules/playwright');
const { mkdirSync, readFileSync, writeFileSync } = require('node:fs');
const { resolve } = require('node:path');
let activeBrowser;

(async () => {
  const browser = await chromium.launch({ headless:true, channel:'msedge' });
  activeBrowser=browser;
  const page = await browser.newPage({viewport:{width:832,height:1050},deviceScaleFactor:1});
  const errors=[];
  page.on('pageerror',e=>{errors.push(e.message);console.error('Browser error:',e.message);});
  await page.addInitScript(() => {
    globalThis.__design=[];
    globalThis.Tweak=class {
      constructor(options){this.options=options;}
      addSelect(object,property,options){globalThis.__design.push({object,property,options,update:this.options.onChange});}
    };
  });
  await page.goto('file:///'+resolve('build/car-companion-review.html').replaceAll('\\','/'));
  const frame=page.frames().find(f=>f!==page.mainFrame());
  await frame.locator('.cc-device').waitFor();
  await frame.waitForFunction(()=>document.querySelectorAll('.cc-nav svg').length===5);
  // The standalone sandbox may run its fragment before Playwright's host stand-in.
  await frame.evaluate(fragment=>{
    const documentCopy=new DOMParser().parseFromString(fragment,'text/html');
    document.getElementById('car-companion').replaceWith(documentCopy.getElementById('car-companion'));
    globalThis.__design=[];
    (0,eval)(documentCopy.querySelector('script').textContent);
  },readFileSync(resolve('docs/product-ui/car-companion.html'),'utf8'));
  const output=resolve('docs/product-ui/previews');
  mkdirSync(output,{recursive:true});
  const report=[];
  const select=async(key,value)=>frame.evaluate(({key,value})=>{
    const control=globalThis.__design.find(c=>c.property===key);
    if(!control)throw new Error('Missing design control '+key);
    control.object[key]=value;control.update();
  },{key,value});
  async function inspect(name,screenshot=true){
    const geometry=await frame.evaluate(()=>{
      const root=document.getElementById('car-companion');
      const device=root.querySelector('.cc-device'),box=device.getBoundingClientRect();
      const problems=[];
      const content=root.querySelector('.cc-content').getBoundingClientRect();
      const nav=root.querySelector('.cc-nav').getBoundingClientRect();
      if(content.bottom>nav.top+1)problems.push('content overlaps navigation');
      if(/\b(?:OBD|CAN|ESP32)\b/i.test(device.innerText))problems.push('technical identifier visible');
      for(const node of root.querySelectorAll('*')){
        if(!node.getClientRects().length||node.closest('[hidden]'))continue;
        const b=node.getBoundingClientRect();
        if(b.left<box.left-1||b.right>box.right+1||b.top<box.top-1||b.bottom>box.bottom+1)problems.push('outside: '+node.className);
        if(!node.children.length && node.textContent.trim() && node.namespaceURI!=='http://www.w3.org/2000/svg'){
          if(node.scrollWidth>node.clientWidth+1&&node.clientWidth>0)problems.push('clipped: '+node.textContent.trim());
        }
      }
      for(const row of root.querySelectorAll('.cc-value-row')){
        const children=[...row.children];
        for(let i=1;i<children.length;i++){
          const previous=children[i-1].getBoundingClientRect(),next=children[i].getBoundingClientRect();
          if(previous.right>next.left+1&&previous.bottom>next.top+1&&next.bottom>previous.top+1)problems.push('value/unit overlap: '+row.textContent);
        }
      }
      return {width:box.width,height:box.height,problems};
    });
    report.push({name,...geometry});
    if(screenshot)await frame.locator('.cc-device').screenshot({path:resolve(output,name+'.png')});
  }
  const pages=['drive','health','tires','trip','performance'];
  if(await frame.locator('.cc-device').getAttribute('data-appearance')!=='dark')throw new Error('Default appearance is not dark');
  await inspect('default-drive');
  for(const [skin,id] of [['夜行霓虹','neon'],['熔岩赛道','lava'],['酸性机能','acid']]){
    await select('skin',skin);
    for(const appearance of ['日间','夜间']){
      await select('appearance',appearance);
      for(const tab of pages){
        await frame.locator(`[data-page="${tab}"]`).click();
        await inspect(`${id==='neon'?'':id+'-'}${appearance==='日间'?'light':'dark'}-${tab}`);
      }
    }
  }
  await select('appearance','夜间');
  await frame.locator('[data-page="drive"]').click();
  await frame.locator('[data-action="style"]').click();
  for(const [skin,id] of [['夜行霓虹','neon'],['熔岩赛道','lava'],['酸性机能','acid']]){
    await frame.getByRole('radio',{name:skin,exact:true}).check();
    if(await frame.locator('.cc-device').getAttribute('data-skin')!==id)throw new Error('Skin radio did not update the display');
    if(await frame.locator('[data-page="drive"]').getAttribute('aria-current')!=='page')throw new Error('Skin change switched page');
    await inspect('style-picker-'+id);
  }
  await frame.locator('[data-action="close"]').click();
  await select('skin','夜行霓虹');
  await select('appearance','日间');
  await select('scenario','待连接');
  for(const tab of pages){
    await frame.locator(`[data-page="${tab}"]`).click();
    await inspect('offline-'+tab);
    if(tab==='drive'){
      if(await frame.locator('.cc-revs span').count()!==32)throw new Error('RPM segments missing');
      if(await frame.locator('.cc-revs [data-lit="true"]').count())throw new Error('Offline RPM is lit');
    }
  }
  await select('scenario','胎压偏低');
  await frame.locator('[data-page="drive"]').click();
  await frame.locator('[data-action="alerts"]').click();
  await inspect('tire-alert');
  await frame.locator('[data-action="view-tires"]').click();
  if(await frame.locator('[data-page="tires"]').getAttribute('aria-current')!=='page')throw new Error('Alert navigation failed');
  await inspect('tire-low');
  await frame.locator('[data-action="tire-settings"]').click();
  await frame.locator('[data-setting="pressure"]').selectOption('kPa');
  await frame.locator('[data-action="close"]').click();
  if(!await frame.locator('.cc-tire').first().innerText().then(t=>t.includes('240')))throw new Error('Pressure conversion failed');
  await frame.locator('[data-page="trip"]').click();
  await frame.locator('[data-period="今日"]').click();
  if(!await frame.locator('.cc-trip-distance').innerText().then(t=>t.includes('82.4')))throw new Error('Trip navigation failed');
  await frame.locator('[data-action="settings"]').click();
  await frame.locator('[data-setting="fuelPrice"]').fill('9.00');
  await frame.locator('[data-setting="appearance"]').selectOption('夜间');
  await frame.locator('[data-action="close"]').click();
  if(!await frame.locator('.cc-trip-stats').innerText().then(t=>t.includes('56.34')))throw new Error('Trip cost update failed');
  await select('powertrain','自然吸气');
  await frame.locator('[data-page="performance"]').click();
  if(!await frame.locator('.cc-boost').innerText().then(t=>t.includes('绝对压力')))throw new Error('Powertrain variant failed');
  await select('scenario','行驶');
  for(const width of [736,360]){
    await page.setViewportSize({width:width+32,height:1400});
    for(const appearance of ['日间','夜间']){
      await select('appearance',appearance);
      for(const tab of pages){await frame.locator(`[data-page="${tab}"]`).click();await inspect(`${width}-${appearance==='日间'?'light':'dark'}-${tab}`);}
    }
  }
  await frame.locator('[data-action="style"]').click();
  await inspect('360-style-picker');
  await frame.locator('[data-action="close"]').click();
  await select('scenario','胎压偏低');
  for(const tab of pages){await frame.locator(`[data-page="${tab}"]`).click();await inspect('360-alert-'+tab);}
  await page.setViewportSize({width:352,height:1400});
  for(const tab of pages){await frame.locator(`[data-page="${tab}"]`).click();await inspect('320-alert-'+tab);}
  writeFileSync(resolve(output,'validation.json'),JSON.stringify({errors,report},null,2));
  console.log(JSON.stringify({errors,report},null,2));
  await browser.close();
  if(errors.length||report.some(r=>r.problems.length))process.exitCode=1;
})().catch(async error=>{console.error(error);if(activeBrowser)await activeBrowser.close();process.exitCode=1;});
