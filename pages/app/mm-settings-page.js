// MobMesh settings for the repeater the app is logged in to: each field shows the repeater's reply as-is, sent over the app's companion link.
import {transportFactory} from '../console/src/transports/browser.js';
import {RemoteProbe,startStream,stopStream} from '../console/src/transports/remote.js';
import {zoneAt} from '../flasher/src/zones.js';

const $=id=>document.getElementById(id);
const params=new URLSearchParams(location.search);
const tap=parent!==self?parent.mmTap:null;
const UNKNOWN=/unknown command/i;
// Upstream `board` names of the ESP32 targets in build-targets.yaml; the nRF52 ones are listed so an unknown name is never guessed.
const ESP32_BOARDS=[/^Heltec V4/,/^Heltec V3$/,/^Heltec Tracker V2$/,/^Xiao C3$/,/^Xiao S3 WIO$/,/^Grumpy Node$/,/^RAK 3112$/,/^Generic ESP32$/,/^Station G3 ESP32$/,/^Waveshare ESP32-S3-LR1121$/];
const NRF52_BOARDS=[/^Heltec T1$/,/^RAK 4631$/,/^RAK 3401$/,/^Heltec T114$/,/^Seeed SenseCap Solar$/,/^Seeed Xiao-nrf52$/,/^Heltec Mesh Solar$/,/^Heltec Tower V2$/];
// Release builds' mods by board and role (build-targets.yaml); null when the board is not one we build, so every card stays.
function releaseMods(board,role){
  const esp32=ESP32_BOARDS.some(r=>r.test(board)),nrf52=NRF52_BOARDS.some(r=>r.test(board));
  if(!esp32&&!nrf52)return null;
  const mods=new Set(['core','direct-retry','sync-settings','timing-safety','try-settings']);
  if(esp32)mods.add('hotspot-ota');
  if(role!=='room')for(const m of ['flood-gate','diag-reports'])mods.add(m);
  if(role!=='room'&&/^Heltec V4|^Heltec Tracker V2$/.test(board))mods.add('rx-duty-cycle');
  // A switchable LNA (KCT8103L front end) is a board fact, not a mod.
  if(/^Heltec V4|^Heltec Tracker V2$/.test(board))mods.add('lna');
  if(role!=='room'&&/^Heltec V4/.test(board))mods.add('power-guard');
  return mods;
}

// Each group: its title, the get that shows whether the build has it, and its fields.
const GROUPS={
  diag:{title:'Diagnostics',probe:'get tz',missing:'This repeater is not running MobMesh firmware.',fields:[
    {label:'Time zone',hint:'Whole hours from UTC, used for local report times.',get:'get tz',input:{placeholder:'e.g. -5',set:v=>'set tz '+v,check:v=>/^[+-]?\d{1,2}$/.test(v)&&+v>=-12&&+v<=14?'':'Whole hours, -12 to +14'}},
    {label:'Summary',mod:'diag-reports',hint:'Links, LNA, interference, noise and battery.',run:'diag'},
    {label:'RF scan',mod:'diag-reports',hint:'Interference within 4 MHz of the mesh frequency; about 5 s.',run:'diag radio.rfscan'},
    {label:'Links',mod:'diag-reports',hint:'Signal margin for each heard neighbour.',run:'diag radio.links'},
    {label:'Noise',mod:'diag-reports',hint:'Noise floor and failed decodes, last 24 hours.',run:'diag radio.noise'},
    {label:'Battery',mod:'diag-reports',hint:'24 hours of battery history.',run:'diag batt'},
    {label:'LNA study',mod:['diag-reports','lna'],hint:'Result of comparing the LNA on and off, or progress while a study runs.',lnastudy:true},
    {label:'Clear history',mod:'diag-reports',hint:'Erase every diagnostic history; the time zone is kept.',run:['clear diag','clear diag confirm'],runLabel:'Clear',confirm:'Clear every diagnostic history on this repeater? This cannot be undone.'}]},
  updates:{title:'Updates',fields:[
    {label:'Regions',mod:'sync-settings',regions:true,hint:'Replace the region list with the current one for this repeater\u2019s zone.'},
    {label:'Wi-Fi',ota:true,mod:'hotspot-ota',hint:'Network for updates. The saved name is not shown; a blank password saves an open network.',wifi:true},
    {label:'Auto updates',ota:true,mod:'hotspot-ota',get:'get ota.wan.auto',toggle:v=>'set ota.wan.auto '+v,hint:'Turning on needs a passed Verify.'},
    {label:'Verify',ota:true,mod:'hotspot-ota',hint:'Checks internet and time over Wi-Fi; run again for the result.',run:'ota wan verify'},
    {label:'Update now',ota:true,mod:'hotspot-ota',hint:'Download and install the newest firmware over Wi-Fi.',run:'start ota wan update',confirm:'Start a firmware update on this repeater now?'},
    {label:'Local upload',ota:true,mod:'hotspot-ota',hint:'Open the repeater’s own Wi-Fi for a nearby upload.',run:'start ota',confirm:'Open the local upload access point on this repeater?'}]},
  radio:{title:'Radio & Power',fields:[
    {label:'RX duty cycle',mod:'rx-duty-cycle',get:'get radio.rxdc',choices:[['Auto','set radio.rxdc auto'],['Off','set radio.rxdc off']],hint:'Sleep the radio between listens while power saving runs.'},
    {label:'Preamble filter',mod:'rx-duty-cycle',get:'get radio.rxdc.timeout',choices:[['On','set radio.rxdc.timeout on'],['Off','set radio.rxdc.timeout off']],hint:'End a listen early after a false preamble.'},
    {label:'RX LNA duty cycle',mod:'rx-duty-cycle',get:'get radio.fem.rxdc',choices:[['Auto','set radio.fem.rxdc on'],['Off','set radio.fem.rxdc off']],hint:'Bypass the LNA while the radio sleeps.'},
    {label:'RX gain (LNA)',mod:'lna',get:'get radio.fem.rxgain',choices:[['On','set radio.fem.rxgain on'],['Off','set radio.fem.rxgain off'],['Auto','set radio.fem.rxgain auto','diag-reports']],hint:'The receive amplifier; Auto learns on or off for each 2-hour block.'},
    {label:'Try radio settings',get:'get radio',tryradio:true,mod:'try-settings',hint:'Applied for the minutes given, then reverts unless you press Keep.'},
    {label:'Power saving',get:'powersaving',choices:[['On','powersaving on'],['Off','powersaving off'],['Auto','powersaving auto on','power-guard']]},
    {label:'Safe mode',mod:'power-guard',get:'powersaving safe',choices:[['On','powersaving safe on'],['Off','powersaving safe off']],hint:'Sleep on low battery until it recovers.'},
    {label:'Reboot',run:'reboot',runLabel:'Reboot',confirm:'Reboot this repeater now?'},
    {label:'Timed power off',mod:'power-guard',minutes:{placeholder:'Minutes, 1-1440',send:m=>'poweroff '+m*60,confirm:m=>'Power this repeater off for '+m+' minutes? It wakes by itself.'},hint:'Deep sleep for a while, then wake.'}]},
  routing:{title:'Routing',fields:[
    {label:'Direct-retry',mod:'direct-retry',get:'get direct.retry',toggle:v=>'set direct.retry '+v,run:'get direct.retry.stats',runLabel:'Stats',hint:'Resend a direct packet when no echo is heard. Stats: resends, echoes and replies since boot.'},
    {label:'Sinkhole',mod:'flood-gate',get:'sinkhole',sinkhole:true,hint:'Temporarily control traffic that might be spamming the mesh using your repeater, or just watch it closely.'}]}
};
const group=GROUPS[params.get('group')]||GROUPS.diag;
$('title').textContent='MobMesh · '+group.title;
$('back').onclick=()=>parent.mmSettingsClose?.();

let probe=null,key=null,busy=false,target=null,mods=null;
const buttons=new Set();
const state=(text,error=false)=>{$('state').textContent=text;$('state').classList.toggle('error',error);$('state').hidden=!text;};
const refresh=()=>{for(const b of buttons)b.disabled=busy||b.blocked?.();};
const el=(tag,cls,text)=>{const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=text;return e;};
const button=(text,onclick,cls='act',blocked=null)=>{const b=el('button',cls,text);b.type='button';b.onclick=onclick;b.blocked=()=>b.off||!!blocked?.();buttons.add(b);return b;};

// One command at a time; rows that arrive after the reply append to the field that sent it.
async function send(command,out){
  if(busy)return;busy=true;refresh();target=out;out.classList.add('wait');out.textContent='Waiting for the repeater…';
  try{const r=await probe.cli(key,command);out.textContent=r.text;return r.text;}
  catch(e){out.textContent=e.message;return null;}
  finally{out.classList.remove('wait');busy=false;refresh();}
}

// The sinkhole card: entered fields build the command shown above Send; the firmware checks it.
// Try radio settings: the fields open with the current radio and build a timed trial; Keep or Revert ends it early.
function tryradio(box,head,out){
  const spec=[['freq','Frequency MHz'],['bw','Bandwidth kHz'],['sf','SF 5-12'],['cr','CR 5-8'],['min','Minutes, 1-1440']],inputs={};
  const row=el('div','inputs'),preview=el('code','preview');
  for(const [k,ph] of spec){const i=el('input');i.placeholder=ph;i.inputMode='decimal';i.autocomplete='off';inputs[k]=i;}
  const v=k=>inputs[k].value.trim();
  const ready=()=>spec.every(([k])=>v(k))&&/^\d+$/.test(v('min'))&&+v('min')>=1&&+v('min')<=1440;
  const command=()=>'tryset '+(+v('min')*60)+' radio '+[v('freq'),v('bw'),v('sf'),v('cr')].join(',');
  const update=()=>{preview.textContent=ready()?command():'Fill every field';refresh();};
  for(const i of Object.values(inputs))i.oninput=update;
  row.append(...Object.values(inputs));
  const sendRow=el('div','inputs');sendRow.append(preview,button('Try',()=>{if(confirm('Try these radio settings for '+v('min')+' minutes? If the repeater stops answering, it returns to its saved settings when the time ends.'))send(command(),out);},'act fill',()=>!ready()));
  head.append(button('Status',()=>send('tryset',out)),button('Keep',()=>send('tryset keep',out)),button('Revert',()=>send('tryset revert',out)));
  box.append(row,sendRow);update();
  // get radio answers freq,bw,sf,cr; the trial length is left for the user.
  return {fill(text){const parts=text.replace(/^\s*>\s*/,'').trim().split(',');if(parts.length>=4){['freq','bw','sf','cr'].forEach((k,n)=>inputs[k].value=parts[n].trim());out.textContent='';}else out.textContent=text;update();}};
}

// Regions: Update regions loads and checks the zone's current list read-only; only a list that passes the check turns the button into Save regions.
function regions(box,head,out){
  let pending=null;
  const preview=el('pre','reply proposed');preview.hidden=true;
  const action=button('Update regions',()=>pending?saveRegions(pending,out,card):loadRegions(out,card),'act fill');head.append(action);
  box.append(preview);
  const card={
    propose(found,problems=[]){
      pending=found&&!problems.length?found:null;
      preview.hidden=!found;
      preview.textContent=found?'Zone '+found.label+'\n'+found.lines.map(l=>l.slice('sync.region def '.length)).join('\n')+(problems.length?'\n\n\u2717 '+problems.join('\n\u2717 '):'\n\n\u2713 Syntax check passed'):'';
      action.textContent=pending?'Save regions':'Update regions';
    }
  };
  return card;
}

// Client-side check of the def lines, following upstream's region def grammar, before anything is sent.
function checkRegions(lines){
  const problems=[],names=[],bytes=t=>new TextEncoder().encode(t);
  for(const line of lines){
    if(bytes(line).length>157)problems.push('Line longer than 157 bytes: '+line);
    const tokens=line.slice('sync.region def '.length).trim().split(/ +/).filter(Boolean);
    if(!tokens.length)problems.push('Empty def line');
    for(const token of tokens){
      const cut=token.search(/[|,]/),name=cut<0?token:token.slice(0,cut),jump=cut<0?null:token.slice(cut+1);
      if(!name)problems.push('Empty name in "'+token+'"');
      else if(bytes(name).length>30)problems.push('Name longer than 30 bytes: '+name);
      else if([...bytes(name)].some(c=>!(c===45||c===36||c===35||(c>=48&&c<=57)||c>=65)))problems.push('Name has a character regions refuse: '+name);
      if(name)names.push(name);
      if(jump!==null&&!jump)problems.push('Empty parent after "'+name+'"');
      else if(jump&&!names.some(n=>n.startsWith(jump)))problems.push('Parent "'+jump+'" is not defined before it is used');
    }
  }
  return problems;
}

// The repeater's position: its advert in the contact record the app read, else its own lat and lon settings.
async function position(){
  const contact=tap.contacts?.get(key);
  if(contact&&contact.length>=144){const v=new DataView(contact.buffer,contact.byteOffset);const lat=v.getInt32(136,true)/1e6,lon=v.getInt32(140,true)/1e6;if(lat||lon)return {lat,lon};}
  const num=t=>parseFloat(String(t).replace(/^\s*>\s*/,''));
  const lat=num((await probe.cli(key,'get lat')).text),lon=num((await probe.cli(key,'get lon')).text);
  return Number.isFinite(lat)&&Number.isFinite(lon)&&(lat||lon)?{lat,lon}:null;
}

// The zone profile's sync-settings branch holds the region lines; profiles are always read fresh, never from a cache.
async function zoneRegions(lat,lon){
  const fresh=async name=>{const r=await fetch(new URL('../flasher/data/'+name,location.href),{cache:'no-store'});if(!r.ok)throw Error(name+': HTTP '+r.status);return r.json();};
  const zone=zoneAt(await fresh('zones.geojson'),lat,lon);
  if(!zone?.settings)return {zone};
  const lines=[],walk=list=>{for(const c of list||[]){if(typeof c==='string'){if(c.startsWith('sync.region def '))lines.push(c);}else if(c&&c.if_mod==='sync-settings')walk(c.then);}};
  walk((await fresh(zone.settings)).commands);
  return {zone,lines};
}

// Step one: find the zone from the repeater's position, read its profile fresh, and show the checked list.
async function loadRegions(out,card){
  if(busy)return;
  // Region sync must be on: with it off the overlay is not used, so there is nothing to update.
  const status=await send('sync.region publish.status',out);
  if(status===null)return;
  if(!/sync:on/.test(status)){out.textContent=/sync:off/.test(status)?'Region sync is off, so the region list is not in use.':status;return;}
  out.textContent='Finding this repeater\u2019s zone\u2026';
  let found;
  let pos;
  try{pos=await position();}catch(e){out.textContent='No reply when reading the repeater\u2019s position: '+e.message;return;}
  if(!pos){out.textContent='This repeater has no position. Set it with Position in the app first.';return;}
  try{found=await zoneRegions(pos.lat,pos.lon);}catch(e){out.textContent='Could not load the zone list: '+e.message;return;}
  if(!found.zone){out.textContent='This repeater\u2019s position is outside every MobMesh zone.';return;}
  const label=found.zone.id+(found.zone.settings?' ('+found.zone.settings.replace(/-settings\.json$/,'')+')':'');
  if(!found.lines?.length){out.textContent='Zone '+label+' has no region list.';return;}
  out.textContent='';
  card.propose({label,lines:found.lines},checkRegions(found.lines));
}

// Each name's depth in the tree a run of def lines produces after a clear, following upstream's cursor rules.
function expectedTree(lines){
  const parent=new Map();
  for(const line of lines){
    let cursor='';
    for(const token of line.slice('sync.region def '.length).trim().split(/ +/).filter(Boolean)){
      const cut=token.search(/[|,]/),name=cut<0?token:token.slice(0,cut),jump=cut<0?null:token.slice(cut+1);
      parent.set(name,cursor);
      cursor=jump?[...parent.keys()].find(n=>n.startsWith(jump))??jump:name;
    }
  }
  const depth=new Map(),of=n=>{let d=0;for(let p=parent.get(n);p&&d<32;p=parent.get(p))d++;return d;};
  for(const n of parent.keys())depth.set(n,of(n));
  return depth;
}

// The working overlay as each name's printed depth, read page by page; null when a page gets no reply.
async function readTree(out){
  let text='',offset=0;
  for(let page=0;page<8;page++){
    const reply=await send('sync.region'+(offset?' '+offset:''),out);
    if(reply===null)return null;
    const next=reply.match(/^\s*next (\d+)\s*$/m);
    text+=(text?'\n':'')+reply.replace(/^\s*next \d+\s*$/m,'').replace(/\s+$/,'');
    if(!next)break;offset=+next[1];
  }
  const depth=new Map();
  for(const line of text.split('\n')){
    const m=line.replace(/^>\s?/,'').match(/^( *)(\S+)( F)?$/);
    if(m&&m[2]!=='empty')depth.set(m[2],m[1].length);
  }
  return {text,depth};
}

// Differences between the expected and read-back trees, in words.
function treeDiff(want,got){
  const out=[];
  for(const [n,d] of want)if(!got.has(n))out.push('missing '+n);else if(got.get(n)!==d)out.push(n+' at level '+got.get(n)+', expected '+d);
  for(const n of got.keys())if(!want.has(n))out.push('unexpected '+n);
  return out;
}

// After a failed run the saved list is loaded back into RAM; if that reload is not confirmed, Retry reload is offered.
async function restore(out,why){
  const text=await send('sync.region reload',out);
  if(text!==null&&!/^\s*err/i.test(text)){out.textContent=why+'\nNothing was saved; the repeater is back on its saved list.';return;}
  out.textContent=why+'\nNothing was saved, but reloading the saved list was not confirmed, so the repeater\u2019s working list may be incomplete until it reloads or reboots.';
  const retry=button('Retry reload',async()=>{retry.remove();buttons.delete(retry);await restore(out,why);},'act danger');
  out.after(retry);refresh();
}

// Step two: clear and each def change only the RAM copy; the tree is read back and compared before save, so only a confirmed list reaches flash.
async function saveRegions(found,out,card){
  if(busy)return;
  card.propose(null);
  for(const command of ['sync.region clear',...found.lines]){
    const text=await send(command,out);
    if(text===null||/^\s*err/i.test(text)){await restore(out,'Stopped at "'+command+'": '+out.textContent);return;}
  }
  const read=await readTree(out);
  if(!read){await restore(out,'The new list could not be read back to check it.');return;}
  const diff=treeDiff(expectedTree(found.lines),read.depth);
  if(diff.length){await restore(out,'The list read back from the repeater does not match: '+diff.join('; ')+'.');return;}
  // save is safe to repeat, so a lost reply is answered by sending it once more.
  let saved=await send('sync.region save',out);
  if(saved===null)saved=await send('sync.region save',out);
  if(saved===null||/^\s*err/i.test(saved)){
    if(saved===null){out.textContent='The checked list is in the repeater\u2019s working copy, but no reply confirmed the save. Press Update regions to check again.';return;}
    await restore(out,'Save failed: '+saved);return;
  }
  out.textContent='Saved zone '+found.label+', checked before saving.\n\nRegion tree:\n'+read.text;
}

// LNA study: Report first; its answer decides whether Stop (a study is running) or Start with a length is offered.
function lnastudy(box,head,out){
  const row=el('div','inputs'),hours=el('input');row.hidden=true;
  hours.placeholder='Hours 6-168 (48 if blank)';hours.inputMode='numeric';
  const ok=()=>{const v=hours.value.trim();return !v||(/^\d+$/.test(v)&&+v>=6&&+v<=168);};
  const start=button('Start',async()=>{const v=hours.value.trim()||'48';if(!confirm('Start a '+v+'-hour LNA study? The LNA switches on and off in short blocks until it ends; the saved setting is not changed.'))return;if(await send('diag radio.lna start'+(hours.value.trim()?' '+v:''),out)!==null)row.hidden=true;},'act fill',()=>!ok());
  const stop=button('Stop',async()=>{if(!confirm('Stop the LNA study? The LNA goes back to how it was before the study.'))return;if(await send('diag radio.lna stop',out)!==null)stop.hidden=true;},'act danger');stop.hidden=true;
  hours.oninput=refresh;row.append(hours,start);
  head.append(button('Report',async()=>{
    row.hidden=stop.hidden=true;
    const text=await send('diag radio.lna',out);
    if(text===null||/^\s*ERR/i.test(text))return;
    if(/Studying/i.test(text))stop.hidden=false;else row.hidden=false;
    refresh();
  }),stop);
  box.append(row);
}

function sinkhole(box,head,out){
  const action=el('select'),inputs={};
  for(const [v,t] of [['add','Block'],['watch','Watch'],['remove','Remove']])action.append(new Option(t,v));
  const row=el('div','inputs'),preview=el('code','preview');
  const spec=[['hash','Hash (a1b2) or *'],['hours','Hours 1-168'],['chan','Channel: public, #name, 0xNN'],['depth','Depth 1-63 or any'],['minbytes','Min bytes 2 or 3']];
  for(const [k,ph] of spec){const i=el('input');i.placeholder=ph;i.autocomplete='off';i.spellcheck=false;inputs[k]=i;}
  const command=()=>{
    const v=k=>inputs[k].value.trim(),remove=action.value==='remove',parts=['sinkhole',action.value,v('hash')];
    if(!remove&&v('hours'))parts.push(v('hours'));
    if(v('chan'))parts.push('chan',v('chan'));
    if(!remove&&v('depth'))parts.push('depth',v('depth'));
    if(!remove&&v('minbytes'))parts.push('minbytes',v('minbytes'));
    return parts.join(' ');
  };
  const update=()=>{const remove=action.value==='remove';for(const k of ['hours','depth','minbytes'])inputs[k].disabled=remove;preview.textContent=inputs.hash.value.trim()?command():'Enter a hash or *';refresh();};
  action.onchange=update;for(const i of Object.values(inputs))i.oninput=update;
  row.append(action,...Object.values(inputs));
  const sendRow=el('div','inputs');sendRow.append(preview,button('Send',()=>send(command(),out),'act fill',()=>!inputs.hash.value.trim()));
  head.append(button('List',()=>send('sinkhole',out)),button('Stats',()=>send('sinkhole stats',out)));
  box.append(row,sendRow);update();
}

function field(f){
  const box=el('section','field'),head=el('div','head'),out=el('pre','reply'),before=new Set(buttons);
  head.append(el('span','label',f.label));box.append(head);
  if(f.hint)box.append(el('div','hint',f.hint));
  // A run of several commands (clear diag, then its confirm) stops at the first that fails.
  // Values are read only on request, as elsewhere in the app: the refresh glyph reads this card's current value.
  if(f.get&&!f.sinkhole){const r=button('',()=>card.load(),'icon refresh');r.title='Get current value';r.setAttribute('aria-label','Get current value');r.innerHTML='<svg viewBox="0 0 24 24" aria-hidden="true"><path d="M20 11a8 8 0 1 0-2.3 5.7M20 4v7h-7"/></svg>';head.append(r);}
  if(f.run)head.append(button(f.runLabel||'Run',async()=>{if(f.confirm&&!confirm(f.confirm))return;for(const c of [].concat(f.run)){const text=await send(c,out);if(text===null||/err|unknown/i.test(text))break;}},f.confirm?'act danger':'act'));
  if(f.toggle)head.append(button('On',()=>send(f.toggle('on'),out)),button('Off',()=>send(f.toggle('off'),out)));
  // A choice tied to a mod is left out where the board's release build lacks that mod.
  if(f.choices)for(const [text,command,mod] of f.choices)if(!mod||!mods||mods.has(mod))head.append(button(text,()=>send(command,out)));
  if(f.minutes){
    const input=el('input');input.placeholder=f.minutes.placeholder;input.inputMode='numeric';
    const ok=()=>/^\d+$/.test(input.value.trim())&&+input.value>=1&&+input.value<=1440;
    const go=button('Power off',()=>{const m=+input.value.trim();if(confirm(f.minutes.confirm(m)))send(f.minutes.send(m),out);},'act danger',()=>!ok());
    input.oninput=refresh;const row=el('div','inputs');row.append(input,go);box.append(row);
  }
  // A value field opens with the repeater's current value; Set lights up only once it is changed.
  let input=null,saved=null,inputRow=null;
  if(f.input){
    input=el('input');input.placeholder=f.input.placeholder;input.inputMode='numeric';
    const set=button('Set',async()=>{
      const v=input.value.trim(),bad=f.input.check(v);if(bad){out.textContent=bad;return;}
      const text=await send(f.input.set(v),out);if(text!==null&&!/err|unknown|invalid/i.test(text))saved=v;refresh();
    },'act fill',()=>!input.value.trim()||input.value.trim()===saved);
    // The field appears only once the current value has been read.
    input.oninput=refresh;inputRow=el('div','inputs');inputRow.hidden=true;inputRow.append(input,set);box.append(inputRow);
  }
  if(f.wifi){
    const ssid=el('input'),pass=el('input');ssid.placeholder='Network name (SSID)';ssid.autocomplete='off';pass.placeholder='Password';pass.type='password';pass.autocomplete='new-password';
    const bytes=s=>new TextEncoder().encode(s).length;
    const set=button('Set',async()=>{
      const s=ssid.value,p=pass.value;
      if(s.includes(','))out.textContent='The network name cannot contain a comma.';
      else if(bytes(s)>31)out.textContent='The network name is limited to 31 bytes.';
      else if(bytes(p)>63)out.textContent='The password is limited to 63 bytes.';
      else if(await send('set ota.wan.wifi '+s+','+p,out)!==null)pass.value='';
      refresh();
    },'act fill',()=>!ssid.value);
    ssid.oninput=pass.oninput=refresh;const row=el('div','inputs');row.append(ssid,pass,set);box.append(row);
  }
  if(f.sinkhole)sinkhole(box,head,out);
  if(f.lnastudy)lnastudy(box,head,out);
  const radio=f.tryradio?tryradio(box,head,out):null,region=f.regions?regions(box,head,out):null;
  box.append(out);
  const own=[...buttons].filter(b=>!before.has(b));
  // load runs the field's opening get, leaving Retry in its header after a timeout; missing keeps the card, says the build lacks it, and greys its buttons.
  const card={box,out,get:f.get,ota:!!f.ota,fill(text){
    if(radio){radio.fill(text);return;}
    const value=input&&text.match(/[-+]?\d+/);
    if(inputRow)inputRow.hidden=false;
    if(value){saved=String(+value[0]);input.value=saved;out.textContent='';refresh();}else out.textContent=text;
  },async load(){
    const text=await send(f.get,out);
    if(text!==null&&!UNKNOWN.test(text))card.fill(text);
    if(text&&UNKNOWN.test(text)){card.missing();return false;}
    if(text===null&&!head.querySelector('.retry')){const r=button('Retry',async()=>{r.remove();buttons.delete(r);await card.load();},'act retry');head.append(r);refresh();}
    return true;
  },missing(){for(const b of own)b.off=true;for(const i of box.querySelectorAll('input,select'))i.disabled=true;out.textContent='Not in this build.';refresh();}};
  return card;
}

async function open(){
  if(!tap?.connected||!tap.send){state('Connect to a companion in the app first.',true);return;}
  key=/^[a-f0-9]{64}$/.test(params.get('key')||'')?params.get('key'):tap.loginKey;
  if(!key){state('Log in to a repeater in the app first.',true);return;}
  const transport=transportFactory({}).shared(tap);
  probe=new RemoteProbe(transport.protocol);
  await startStream(transport.protocol,key,text=>{if(target)target.textContent+=(target.textContent?'\n':'')+text;},()=>document.visibilityState!=='hidden');
  addEventListener('pagehide',()=>{stopStream();transport.close();},{once:true});
  await check();
}

// A check with no reply offers Retry; the board name and the group's own get are asked again.
function failed(text){
  state(text,true);
  const retry=el('button','act','Retry');retry.type='button';retry.onclick=()=>{retry.remove();check().catch(e=>state(e.message,true));};
  $('state').append(' ',retry);
}

async function check(){
  state('Checking this repeater\u2026');
  // The board name, asked once per repeater, says which cards its release build carries.
  const contact=tap.contacts?.get(key),role=contact?.[33]===3?'room':contact?.[33]===4?'sensor':'repeater';
  let board=parent.mmSettingsBoard?.get(key);
  if(board==null){try{board=(await probe.cli(key,'board')).text.replace(/^\s*>\s*/,'').trim();parent.mmSettingsBoard?.set(key,board);}catch(e){failed('No reply from the repeater: '+e.message);return;}}
  mods=releaseMods(board,role);
  state('');
  const built=group.fields.filter(f=>!mods||!f.mod||[].concat(f.mod).every(m=>mods.has(m))).map(field);
  $('fields').replaceChildren(...built.map(b=>b.box));refresh();
  if(!built.length)state('Nothing on this page applies to this repeater\u2019s build.');
}
open().catch(e=>state(e.message,true));
