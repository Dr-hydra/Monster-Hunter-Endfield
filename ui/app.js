'use strict';
(() => {
  const $ = id => document.getElementById(id);
  const host = window.betterEndfield;
  const defaults = {enabled:true,toggle_key:119,speed:1,blend_seconds:0.12,buffer_seconds:0.3,root_motion:true,show_prop:true,spirit_key:6,spirit_alt_key:17,sound_enabled:true,sound_volume:0.65,grip_strength:1,local_practice:false,practice_hp:1000,practice_damage:100,practice_radius:0.65,practice_height:0.9,practice_blade_length:1.25};
  let configuration = {...defaults};
  let pendingSave = false;
  const phaseLabels = {idle:'待机',slash:'普通斩',thrust:'突刺',spirit:'气刃连段',fade:'袈裟斩',foresight:'见切斩',sheathe:'特殊纳刀',normal_sheathe:'普通纳刀',iai:'居合拔刀斩',iai_spirit:'居合气刃斩',helm:'登龙',dodge:'翻滚',sheathed:'已纳刀',sheathe_hold:'纳刀待机'};
  const soundStatus = {ready:null,detail:''};
  const logLines = [];
  function log(value) {
    logLines.unshift(`${new Date().toLocaleTimeString()}  ${typeof value === 'string' ? value : JSON.stringify(value)}`);
    $('log').textContent = logLines.slice(0,12).join('\n');
  }
  function renderStatus(body) {
    if (!body || typeof body !== 'object') return;
    if(typeof body.ground_detail==='string')$('ground-detail').textContent=body.ground_detail;
    if(typeof body.last_sound_label==='string')soundStatus.last=body.last_sound_label;
    if(Number.isFinite(body.sound_cues))soundStatus.count=body.sound_cues;
    if ('runtime_ready' in body) { $('runtime-state').textContent = body.runtime_ready ? '已就绪' : '等待游戏'; $('runtime-state').classList.toggle('good',!!body.runtime_ready); }
    if ('active' in body) { $('active-state').textContent = body.active ? (body.auto_demo ? '自动演示中' : '手动演示中') : '已结束'; $('active-state').classList.toggle('good',!!body.active); }
    if ('character' in body) $('character-state').textContent = body.character === 'chr_0003_endminf' ? '女管理员' : body.character || '尚未识别';
    if ('slot' in body) $('slot-state').textContent = Number.isInteger(body.slot) && body.slot >= 0 ? `#${body.slot}${body.phase ? ` · ${phaseLabels[body.phase] || '动作中'}` : ''}` : '—';
    if (typeof body.detail === 'string') $('detail').textContent = body.detail;
    if (typeof body.demo_seconds === 'number') $('demo-info').textContent = `自动演示每轮约 ${Math.round(body.demo_seconds)} 秒：普通斩 → 气刃 → 见切 → 居合 → 登龙 → 收势；攻击键可接管。`;
    if (typeof body.weapon_mode === 'string') $('weapon-state').textContent = ({equipped:'装备长剑 · 保持握持',fallback:'备用演示刀',none:'等待演示'})[body.weapon_mode] || '等待演示';
    if (typeof body.weapon_pose_detail === 'string') {
      $('weapon-state').title = body.weapon_pose_detail;
      if (body.weapon_mode === 'equipped') $('weapon-state').textContent += body.blade_calibrated ? ' · 剑身已识别' : ' · 剑身待识别';
    }
    if ('sound_ready' in body) soundStatus.ready=!!body.sound_ready;
    if (typeof body.sound_detail === 'string') soundStatus.detail=body.sound_detail;
    renderSoundStatus();
    if (body.practice && typeof body.practice==='object') {
      const p=body.practice;
      $('practice-detail').textContent=String(p.detail || '本地练习未开启');
      $('practice-stats').textContent=`命中 ${Number(p.hits)||0} 次 · 模拟伤害 ${Math.round(Number(p.total)||0)}${p.last_target ? ` · 最近：${p.last_target} −${Math.round(Number(p.last_damage)||0)}` : ''}`;
      $('practice-targets').textContent=Array.isArray(p.targets)&&p.targets.length ? `${p.summary?'本轮最后记录：':''}${p.targets.map(t=>`${t.name}: ${Math.round(Number(t.hp)||0)} / ${Number(t.max_hp)||0}${t.hp<=0?'（练习完成）':''}`).join('；')}` : '暂无目标';
    }
  }
  function renderSoundStatus() {
    $('sound-state').textContent=!configuration.sound_enabled ? '已关闭' : soundStatus.ready===true ? '音源已载入' : soundStatus.ready===false ? '音效未就绪' : '等待音源';
    $('sound-state').classList.toggle('good',configuration.sound_enabled && soundStatus.ready===true);
    $('sound-detail').textContent=!configuration.sound_enabled ? '太刀音效已关闭。' : (soundStatus.detail || '音效状态会在连接游戏后显示；网页可独立试听。')+(soundStatus.last ? ` · 最近音效 ${soundStatus.last} · 已触发 ${soundStatus.count||0} 次` : '');
  }
  async function send(body) {
    if (!host || typeof host.send !== 'function') { $('detail').textContent = '当前为本地预览。在 E Mod Loader 中打开页面后，按钮可控制游戏内动作。'; return; }
    try { await host.send(body); log(`已发送：${JSON.stringify(body)}`); if(body.action!=='status') $('detail').textContent='已排队，切回游戏后执行。'; }
    catch (error) { $('detail').textContent = `发送失败：${error.message}`; log(error.message); }
  }
  async function refresh() {
    if (!host || typeof host.status !== 'function') { $('connection').textContent = '本地预览'; $('runtime-state').textContent = '未连接'; return; }
    try { const state = await host.status(); $('connection').textContent = state.connected ? '游戏桥接已连接' : '游戏桥接未连接'; $('connection').classList.toggle('connected',!!state.connected); await send({action:'status'}); }
    catch (error) { $('connection').textContent = '连接状态不可用'; log(error.message); }
  }
  function fillForm() {
    $('enabled').checked = !!configuration.enabled;
    $('speed').value = configuration.speed;
    $('speed-value').textContent = `${Number(configuration.speed).toFixed(2)}×`;
    $('blend').value = configuration.blend_seconds;
    $('buffer').value = configuration.buffer_seconds;
    $('root-motion').checked = !!configuration.root_motion;
    $('show-prop').checked = !!configuration.show_prop;
    $('grip-strength').value=configuration.grip_strength;
    $('grip-strength-value').textContent=`${Math.round(Number(configuration.grip_strength)*100)}%`;
    $('local-practice').checked=!!configuration.local_practice;
    $('practice-hp').value=configuration.practice_hp;
    $('practice-damage').value=configuration.practice_damage;
    $('practice-blade-length').value=configuration.practice_blade_length;
    $('practice-radius').value=configuration.practice_radius;
    $('practice-height').value=configuration.practice_height;
    $('sound-enabled').checked = !!configuration.sound_enabled;
    $('sound-volume').value=Number.isFinite(Number(configuration.sound_volume)) ? Math.min(1,Math.max(0,Number(configuration.sound_volume))) : defaults.sound_volume;
    updatePreviewAudio();
    renderSoundStatus();
  }
  $('speed').oninput = () => { $('speed-value').textContent = `${Number($('speed').value).toFixed(2)}×`; };
  $('grip-strength').oninput=()=>{$('grip-strength-value').textContent=`${Math.round(Number($('grip-strength').value)*100)}%`;};
  $('settings').onsubmit = async event => {
    event.preventDefault();
    if (pendingSave || !$('settings').reportValidity()) return;
    const next = {...configuration,enabled:$('enabled').checked,speed:Number($('speed').value),blend_seconds:Number($('blend').value),buffer_seconds:Number($('buffer').value),root_motion:$('root-motion').checked,show_prop:$('show-prop').checked,sound_enabled:$('sound-enabled').checked,sound_volume:Number($('sound-volume').value)};
    Object.assign(next,{grip_strength:Number($('grip-strength').value),local_practice:$('local-practice').checked,practice_hp:Number($('practice-hp').value),practice_damage:Number($('practice-damage').value)});
    Object.assign(next,{practice_blade_length:Number($('practice-blade-length').value),practice_radius:Number($('practice-radius').value),practice_height:Number($('practice-height').value)});
    pendingSave = true;
    try { if (host && typeof host.saveConfig === 'function') { await host.saveConfig(next); $('save-state').textContent = '已保存'; } else { localStorage.setItem('longsword.configuration',JSON.stringify(next)); $('save-state').textContent = '已保存在此浏览器'; } configuration = next; renderSoundStatus(); }
    catch (error) { $('save-state').textContent = `保存失败：${error.message}`; }
    finally { pendingSave = false; }
  };
  $('toggle').onclick = () => send({action:'toggle'});
  $('demo').onclick = () => send({action:'demo'});
  $('stop').onclick = () => send({action:'stop'});
  $('refresh').onclick = refresh;
  document.querySelectorAll('[data-command]').forEach(button => { button.onclick = () => send({action:'command',name:button.dataset.command}); });
  document.querySelectorAll('[data-action]').forEach(button => { button.onclick = () => send({action:button.dataset.action}); });
  $('slot-form').onsubmit = event => { event.preventDefault(); if ($('slot-form').reportValidity()) send({action:'preview',slot:Number($('slot').value)}); };
  const audioButtons=Array.from(document.querySelectorAll('[data-audio]'));
  const audioPlayers=audioButtons.map(button=>$(button.dataset.audio));
  let audition=0;
  function updatePreviewAudio() {
    const volume=Number($('sound-volume').value),enabled=$('sound-enabled').checked;
    $('sound-volume-value').textContent=`${Math.round(volume*100)}%`;
    audioPlayers.forEach(player=>{player.volume=volume;if(!enabled)player.pause();});
    audioButtons.forEach(button=>{button.disabled=!enabled;});
    if(!enabled) { audition++; $('audio-preview-state').textContent='音效已关闭，启用后可试听。'; }
    else if($('audio-preview-state').textContent==='音效已关闭，启用后可试听。') $('audio-preview-state').textContent='试听只播放声音，不驱动游戏动作。';
  }
  $('sound-volume').oninput=updatePreviewAudio;
  $('sound-enabled').onchange=updatePreviewAudio;
  audioButtons.forEach(button=>{
    const player=$(button.dataset.audio);
    button.onclick=async()=>{
      const request=++audition;
      audioPlayers.forEach(other=>{other.pause();other.currentTime=0;});
      $('audio-preview-state').textContent=`正在载入：${button.dataset.soundLabel}`;
      try { await player.play();if(request===audition)$('audio-preview-state').textContent=`正在试听：${button.dataset.soundLabel}`; }
      catch { if(request===audition)$('audio-preview-state').textContent='试听不可用，请检查音频文件和设备。'; }
    };
    player.onended=()=>{if(audioPlayers.every(other=>other.paused))$('audio-preview-state').textContent='试听完成；切回游戏可体验连段音效。';};
  });
  if (host && typeof host.onmessage === 'function') host.onmessage(message => {
    if (message.kind === 'reply' || message.kind === 'event') {
      let body = message.body;
      if (typeof body === 'string') { try { body = JSON.parse(body); } catch { /* textual response */ } }
      renderStatus(body);
      if (message.kind === 'reply' && message.result !== undefined && message.result !== 0) $('detail').textContent = body?.error || body?.detail || `请求未完成（${message.result}）。请查看动作检查。`;
      log(message);
    }
  });

  const canvas = $('motion'), context = canvas.getContext('2d');
  let preview = null, currentClip = 0, paused = false, elapsed = 0, lastTime = null;
  function loadPreview(data) {
    if (!data || !Array.isArray(data.clips) || !Array.isArray(data.bones) || !data.clips.length) throw new Error('预览文件需包含 bones 和 clips');
    const clips = data.clips.filter(clip => Array.isArray(clip.frames) && clip.frames.length && clip.frames.every(frame => Array.isArray(frame) && frame.length === data.bones.length && frame.every(point => Array.isArray(point) && point.length === 3 && point.every(Number.isFinite))));
    if (!clips.length) throw new Error('没有可播放的预览帧');
    preview = {...data,clips}; currentClip = 0; elapsed = 0;
    $('preview-clip').replaceChildren(...clips.map((clip,index) => { const option = document.createElement('option'); option.value = String(index); option.textContent = clip.label || `源动作 #${clip.slot}`; return option; }));
    $('preview-clip').disabled = false;
    $('preview-note').textContent = '源骨骼离线预览 · 女管理员姿势、握刀与实际位移需在游戏中检查。';
    $('load-preview').hidden = true;
  }
  $('preview-clip').onchange = () => { currentClip = Number($('preview-clip').value); elapsed = 0; };
  $('pause').onclick = () => { paused = !paused; $('pause').textContent = paused ? '继续' : '暂停'; $('pause').setAttribute('aria-label',paused ? '继续预览' : '暂停预览'); };
  $('preview-file').onchange = async () => {
    try { if ($('preview-file').files[0]) loadPreview(JSON.parse(await $('preview-file').files[0].text())); }
    catch (error) { $('preview-note').textContent = error.message; }
  };
  function renderPreview(time) {
    if (lastTime !== null && !paused) elapsed += Math.min((time-lastTime)/1000,0.1) * Number($('speed').value);
    lastTime = time;
    const rect = canvas.getBoundingClientRect(), ratio = window.devicePixelRatio || 1;
    if (canvas.width !== Math.round(rect.width*ratio) || canvas.height !== Math.round(rect.height*ratio)) { canvas.width = Math.round(rect.width*ratio); canvas.height = Math.round(rect.height*ratio); }
    context.setTransform(ratio,0,0,ratio,0,0); context.clearRect(0,0,rect.width,rect.height);
    const w=rect.width,h=rect.height;
    context.strokeStyle='#33412b'; context.lineWidth=1;
    for(let i=-3;i<=3;i++) { context.beginPath(); context.moveTo(w/2+i*30,h*.87-30); context.lineTo(w/2+i*45,h*.87+15); context.stroke(); }
    context.beginPath();context.moveTo(w*.12,h*.87);context.lineTo(w*.88,h*.87);context.stroke();
    if (preview) {
      const clip=preview.clips[currentClip],fps=clip.fps || preview.fps || 60;
      const frameIndex=Number.isFinite(clip.duration) && clip.duration>0 ? Math.min(clip.frames.length-1,Math.floor((elapsed%clip.duration)/clip.duration*clip.frames.length)) : Math.floor(elapsed*fps)%clip.frames.length;
      const frame=clip.frames[frameIndex];
      const up=preview.up_axis === 'y' ? 1 : 2, depth=up===1?2:1;
      const projected=frame.map(point=>[point[0]*.91+point[depth]*.35,point[up]-point[depth]*.16]);
      // Keep a stable view across frames instead of fitting each pose independently.
      if(!clip._bounds) {
        let minX=Infinity,maxX=-Infinity,minY=Infinity,maxY=-Infinity;
        clip.frames.forEach(pose=>pose.forEach(point=>{const x=point[0]*.91+point[depth]*.35,y=point[up]-point[depth]*.16;minX=Math.min(minX,x);maxX=Math.max(maxX,x);minY=Math.min(minY,y);maxY=Math.max(maxY,y);}));
        clip._bounds=[minX,maxX,minY,maxY];
      }
      const [minX,maxX,minY,maxY]=clip._bounds;
      const scale=Math.min(w*.8/Math.max(maxX-minX,.001),h*.8/Math.max(maxY-minY,.001));
      const points=projected.map(point=>[(point[0]-(minX+maxX)/2)*scale+w/2,h*.89-(point[1]-minY)*scale]);
      context.lineWidth=2.4;context.lineCap='round';
      preview.bones.forEach((bone,index)=>{const parent=bone.parent;if(Number.isInteger(parent)&&parent>=0&&parent<points.length&&parent!==index){context.strokeStyle=bone.name?.toLowerCase().includes('weapon')?'#eee6ac':'#b3cd81';context.beginPath();context.moveTo(...points[parent]);context.lineTo(...points[index]);context.stroke();}});
      context.fillStyle='#e6f2bf';points.forEach(point=>{context.beginPath();context.arc(...point,2.1,0,Math.PI*2);context.fill();});
      $('preview-title').textContent = `源骨骼 · ${clip.label || `#${clip.slot}`}`;
    } else {
      context.fillStyle='#8c9a81';context.font='12px "Microsoft YaHei",sans-serif';context.textAlign='center';context.fillText('动作预览正在准备',w/2,h*.5);
    }
    requestAnimationFrame(renderPreview);
  }
  requestAnimationFrame(renderPreview);
  (async()=>{
    try { const saved=host && typeof host.readConfig === 'function' ? await host.readConfig() : JSON.parse(localStorage.getItem('longsword.configuration') || '{}'); configuration={...defaults,...saved};fillForm(); }
    catch(error) { fillForm(); log(`读取设置失败：${error.message}`); }
    await refresh();
    try {
      if (window.LONGSWORD_PREVIEW) loadPreview(window.LONGSWORD_PREVIEW);
      else { const response=await fetch('../assets/ui_preview.json');if(!response.ok)throw new Error('预览资源尚未生成');loadPreview(await response.json()); }
    } catch { $('preview-clip').replaceChildren(new Option('暂无离线预览',''));$('preview-note').textContent='直接打开网页时，如浏览器阻止本地读取，可选择 assets/ui_preview.json；完整安装包会自动载入。'; }
  })();
})();
