import {localCommand,localComplete,THEMES,companionCommand,companionComplete,COMPANION_COMMANDS} from './core/local-commands.js';
import {startMeshBackground} from './core/mesh-bg.js';
import {routeText} from './core/route.js';
import {deferredObserver} from './core/observer.js';
import {CredentialVault} from './storage/credentials.js';
import {initializeCompanion,authenticateRepeater} from './transports/diagnostic-engine.js';
import {TemporaryTargets} from './transports/temporary-targets.js';
import {loadCatalog} from './core/catalog.js';import {complete} from './core/completion.js';import {helpRequest,helpText,helpOverview,HELP_KEY_WIDTH} from './core/help.js';import {History} from './core/editor.js';import {wireInput} from './core/grammar.js';import {sensitive,safeDraft,safeRecord} from './core/redaction.js';
// Nothing is persisted yet, so the transcript shows secrets as typed; redaction applies once sessions are saved.
const PERSISTENT=false;const recordText=(text,secret=false)=>PERSISTENT?safeRecord(text,secret):text;import {ConnectionStatus} from './core/connection-status.js';import {retained} from './core/output.js';import {transportFactory} from './transports/browser.js';import {directCommand,displayReply} from './transports/direct.js';import {RemoteProbe,RemoteReplyTimeout,packetCounters,keyBytes} from './transports/remote.js';import Constants from './vendor/meshcore/constants.js';import {fullKey,frameSummary,directReply} from './protocol.js';
const $=id=>document.getElementById(id),status=new ConnectionStatus(),writerId=crypto.randomUUID(),credentials=new CredentialVault();
let passwordRevision=0,linkTx=0,linkRx=0;const activityTimers={};
// Plain bytes until 10,000, then KB (and MB past 10,000 KB) so the status bar stays short.
function byteCount(n){if(n<10000)return n.toLocaleString()+' B';const kb=n/1000;if(kb<10000)return (kb<100?kb.toFixed(1):Math.round(kb).toLocaleString())+' KB';const mb=kb/1000;return (mb<100?mb.toFixed(1):Math.round(mb).toLocaleString())+' MB';}
function activity(event){if(event.direction==='TX')linkTx+=event.bytes;else linkRx+=event.bytes;const direction=event.direction.toLowerCase();$(direction+'-count').textContent=byteCount(event.direction==='TX'?linkTx:linkRx);$(direction+'-activity').dataset.active='true';clearTimeout(activityTimers[direction]);activityTimers[direction]=setTimeout(()=>{$(direction+'-activity').dataset.active='false';},180);}
let catalog,transport=null,factory=null,mode='direct',contacts=[],targetKey=null,session=null,records=[],history=new History(),writable=false,auth=false,busy=false,blocked=false,region=false,sequence=0,epoch=0,abort=null,tabState=null,escapeTab=false;
let temporaryTargets=new TemporaryTargets(),directName=null,stepLine=null;
// The last chosen port or Bluetooth device and repeater, so reconnect can skip the chooser and keep the transcript.
let lastLink=null,keptHistory=null;
// The status bar shows the route to the selected repeater; it follows path-updated notices, flood sends and contact re-reads.
let routeNote=null,routeStale=false;
function routeLabel(){
 if(!transport?.connected)return 'No device selected';
 if(mode==='direct')return 'Route: direct USB';
 if(routeNote)return routeNote;
 const contact=contacts.find(c=>fullKey(c.publicKey)===targetKey);
 return contact?routeText(contact.outPathLen,contact.outPath,contacts):'No repeater selected';
}
function watchRoute(protocol){protocol.on('wire',frame=>{
 if(frame[0]===129&&fullKey(frame.slice(1,33))===targetKey){routeStale=true;add('transport','Companion learned a new route to '+targetKey.slice(0,12));refreshRoute();}
 else if(frame[0]===6&&frame[1]===1&&targetKey){routeNote='Route: flooding · relearning';update();routeStale=true;}
});}
async function refreshRoute(){if(!routeStale||busy||!transport?.protocol)return;routeStale=false;try{await readContacts();routeNote=null;const contact=contacts.find(c=>fullKey(c.publicKey)===targetKey);if(contact)add('transport',routeText(contact.outPathLen,contact.outPath,contacts));}catch(e){routeStale=true;}update();}
const route=()=>({transport:mode,gatewayKey:transport?.gatewayKey||null});
function update(){status.update({deviceConnected:!!transport?.connected,linkState:transport?.connected?'open':busy&&!transport?'connecting':'disconnected',targetKey,targetState:blocked?'unavailable':targetKey?(auth?'ready':'selected'):'none',canSubmit:!!transport?.connected&&!!session&&writable&&auth&&!busy&&!blocked,persistence:PERSISTENT?'enabled':'disabled'});$('mode').disabled=!!transport||busy;for(const id of ['mode-usb','mode-bluetooth'])$(id).disabled=!!transport||busy||!catalog;$('target').disabled=busy;$('refresh-contacts').disabled=busy||!transport?.protocol;$('connect-key').disabled=busy||!transport?.protocol;$('manual-key').disabled=busy||!transport?.protocol;$('login').disabled=busy||!targetKey;$('password').disabled=busy;$('remember-password').disabled=busy;$('forget-password').disabled=busy||!targetKey;$('packet-stats').disabled=busy||!transport?.protocol;$('send').disabled=busy;$('cancel').disabled=!busy;$('identity').textContent=routeLabel();$('identity').title=targetKey||'';const selected=contacts.find(c=>fullKey(c.publicKey)===targetKey);$('prompt-host').textContent=auth?(selected?.advName||directName||'repeater'):'mobmesh';$('connection-panel').dataset.connected=String(!!transport?.connected);$('connection-label').textContent=transport?.connected?(selected?.advName||directName||'Device connected'):'Connect to console';$('connection-feedback').textContent=busy?'Working…':auth?'Authenticated · ready':transport?'Select a repeater and log in':'Ready to connect';$('device-led').dataset.active=String(!!transport?.connected);$('admin-led').dataset.active=String(auth&&!blocked);$('link-kind').textContent=transport?.connected?(mode==='ble'?'Bluetooth':'USB'):'Link';$('device-state').textContent=transport?.connected?(mode==='ble'?'BLUETOOTH':'USB LINK'):'OFFLINE';$('admin-state').textContent=blocked&&auth?'CONSOLE PAUSED':auth?'ADMIN VERIFIED':targetKey?'LOGIN REQUIRED':'NO CONSOLE';$('radio-refresh').disabled=busy||!transport?.protocol;$('reauthenticate').hidden=!blocked||!transport?.connected||mode==='direct';$('reauthenticate').disabled=busy;$('command').placeholder=blocked?'Reauthenticate to continue…':'Type a command…';resizePrompt();}
status.subscribe(s=>{$('status').textContent=s.deviceConnected?(busy?'Working…':blocked?'Console paused':auth?'Ready':'Connected · select a repeater'):'Disconnected';$('status').dataset.connected=String(s.deviceConnected);$('storage').textContent='Session data: memory only';});
function resizePrompt(){const input=$('command');if(input?.style){input.style.height='auto';input.style.height=Math.max(29,input.scrollHeight)+'px';}}
function showConnection(){const dialog=$('connection-dialog');if(!dialog.open){dialog.showModal?.();if(!$('mode-usb').disabled)$('mode-usb').focus();}}
function closeConnection(){ $('connection-dialog').close?.();$('command').focus();}
const commandPrefix=record=>record.kind==='command'?(record.promptHost||'mobmesh')+' ❯ ':'';
function display(){const scroll=$('terminal-scroll'),atBottom=scroll.scrollTop+scroll.clientHeight>=scroll.scrollHeight-70;const visible=records.filter(r=>!['transport','pending'].includes(r.kind));$('pending').hidden=!records.some(r=>r.kind==='pending');$('output').textContent=visible.map(r=>commandPrefix(r)+r.safeText).join('\n');highlightTranscript(visible);$('diagnostic-log').replaceChildren(...records.filter(r=>r.kind==='transport').flatMap(r=>r.safeText.split('\n')).map(line=>{const row=document.createElement('span');row.className='diagnostic-line';row.textContent=line;return row;}));$('welcome').hidden=visible.length>0;if(atBottom)scroll.scrollTop=scroll.scrollHeight;resizePrompt();}
function highlightTranscript(visible){
 if(!globalThis.CSS?.highlights||!globalThis.Highlight||!document.createRange||!$('output').firstChild)return;
 const groups=new Map(),node=$('output').firstChild;let offset=0;
 const mark=(name,start,end)=>{if(end<=start)return;const range=document.createRange();range.setStart(node,start);range.setEnd(node,end);if(!groups.has(name))groups.set(name,[]);groups.get(name).push(range);};
 for(const record of visible){const prefix=commandPrefix(record),text=prefix+record.safeText;
  if(record.kind==='command'){const entry=catalog.resolve(record.safeText),commandStart=offset+prefix.length;mark('cli-prompt-host',offset,offset+prefix.length-3);mark('cli-prompt-symbol',offset+prefix.length-2,commandStart);mark('cli-command',commandStart,commandStart+(entry?.path.join(' ').length||record.safeText.split(' ')[0].length));mark('cli-argument',commandStart+(entry?.path.join(' ').length||0),offset+text.length);}
  if(record.kind==='help-overview'){let at=offset;for(const line of text.split('\n')){mark('cli-help',at,at+HELP_KEY_WIDTH);mark('cli-help-prose',at+HELP_KEY_WIDTH,at+line.length);at+=line.length+1;}}
  if(record.kind==='completion')mark('cli-help',offset,offset+text.split('\n')[0].length);
  // Help blocks are a syntax line then prose; the prose is dimmed so the syntax stands out.
  if(record.kind==='local-help'){let at=offset;for(const block of text.split('\n\n')){const first=block.indexOf('\n'),end=first<0?block.length:first;mark('cli-help',at,at+end);if(first>=0)mark('cli-help-prose',at+first+1,at+block.length);at+=block.length+2;}}
  if(record.kind==='status'&&/stopped|timed out|failed|uncertain|disconnected/i.test(text))mark('cli-error',offset,offset+text.length);
  else if(record.kind==='status'&&/verified/i.test(text))mark('cli-success',offset,offset+text.split('\n')[0].length);
  for(const match of text.matchAll(/\b[a-f0-9]{64}\b/gi))mark('cli-key',offset+match.index,offset+match.index+match[0].length);
  if(record.kind==='command')for(const match of text.matchAll(/\b\d+(?:\.\d+)?\b/g))mark('cli-number',offset+match.index,offset+match.index+match[0].length);
  offset+=text.length+1;
 }
 for(const name of ['cli-prompt-host','cli-prompt-symbol','cli-help-prose','cli-command','cli-argument','cli-number','cli-help','cli-error','cli-key','cli-success'])CSS.highlights.set(name,new Highlight(...(groups.get(name)||[])));
}
async function restorePassword(){const key=targetKey,revision=++passwordRevision;$('password').value='';$('credential-status').textContent='Saved on this browser only.';if(!key||mode==='direct')return;const password=await credentials.read(key);if(targetKey!==key||passwordRevision!==revision)return;if(password){$('password').value=password;$('credential-status').textContent='Saved password recalled for this repeater.';}else if(credentials.error)$('credential-status').textContent='Password storage unavailable in this browser.';}
function add(kind,text,secret=false){const r={eventId:crypto.randomUUID(),sessionId:session?.sessionId||'provisional',writerId,sequence:sequence++,timestamp:Date.now(),kind,safeText:recordText(text,secret),promptHost:kind==='command'?$('prompt-host').textContent:null,targetKey,route:route(),redacted:secret};records.push(r);const keep=retained(records);records=keep.records;if(keep.truncated)$('assistance').textContent='Oldest output records truncated at 2 MiB.';display();return r;}
function live(text){$('live').textContent=($('live').textContent+text).slice(-65536);}
async function release(){writable=false;}
async function chooseSession(){session={sessionId:crypto.randomUUID(),targetKey};writable=true;history=keptHistory||new History();keptHistory=null;update();$('command').focus();}
async function offerSessions(){session=null;writable=false;history=new History();if(mode==='direct')await chooseSession();else{await restorePassword();update();}}
// Direct serial has no contact list, so the prompt name comes from the repeater itself.
// Theme is a per-viewer preference; storage may be unavailable, so the default always works.
function applyTheme(name){if(name==='default')delete document.documentElement.dataset.theme;else document.documentElement.dataset.theme=name;}
function currentTheme(){return document.documentElement.dataset?.theme||'default';}
// The mesh background is a per-viewer preference; it is on unless turned off.
const meshBackground=startMeshBackground($('mesh-canvas'));
function setBackground(on){on?meshBackground?.start():meshBackground?.stop();}
let backgroundOn=true;try{backgroundOn=localStorage.getItem('console-background')!=='off';}catch{}
setBackground(backgroundOn);
try{const saved=localStorage.getItem('console-theme');if(THEMES.includes(saved))applyTheme(saved);}catch{}
// F1 shows the owner's manual command reference from the copy kept beside the page.
const MANUAL_SOURCES=['owners-manual.md'],MANUAL_HEADING='## 9. Complete command and syntax reference';
let manualLoaded=false;
async function loadManual(){
 const {marked}=await import('../../shared/vendor/marked/marked.esm.js');
 let text=null;for(const source of MANUAL_SOURCES){try{const response=await fetch(new URL(source,location.href));if(response.ok){text=await response.text();break;}}catch{}}
 if(text===null)throw Error('Owner\'s manual unavailable');
 const start=text.indexOf(MANUAL_HEADING);if(start<0)throw Error('Command reference section not found in the manual');
 const rest=text.slice(start+MANUAL_HEADING.length),next=rest.search(/\n## /);
 const body=$('manual-enhanced');body.innerHTML=marked.parse(text.slice(start,next<0?undefined:start+MANUAL_HEADING.length+next));
 // The reference stays self-contained: links become plain text, minus their underline and outbound arrow.
 for(const link of body.querySelectorAll('a')){for(const u of link.querySelectorAll('u'))u.replaceWith(...u.childNodes);link.replaceWith(...link.childNodes);}
 for(const node of [...body.querySelectorAll('*')].flatMap(e=>[...e.childNodes]).filter(n=>n.nodeType===3&&n.textContent.includes('↗')))node.textContent=node.textContent.replaceAll('↗','');
 $('manual-native').replaceChildren(...nativeReference());
}
// Until the user moves or resizes it, the reference keeps the panels' top anchor and stretches to just above the status bar.
function placeManual(){const panel=$('manual'),area=document.querySelector('.terminal')?.getBoundingClientRect();if(!area||panel.dataset.placed==='user')return;panel.style.top='';panel.style.bottom='auto';const top=panel.getBoundingClientRect().top;panel.style.height=Math.round(area.bottom-16-top)+'px';}
window.addEventListener('resize',()=>{if(!$('manual').hidden)placeManual();});
// Native commands come from the console's own catalog, so the table always matches completion; MobMesh overrides are left to the manual above.
function nativeReference(){
 const el=(tag,text)=>{const node=document.createElement(tag);if(text!==undefined)node.textContent=text;return node;};
 const overridden=new Set(catalog.entries.filter(e=>e.origin==='mobmesh').map(e=>e.path.join(' ')));
 const rows=catalog.entries.filter(e=>e.origin==='upstream'&&!overridden.has(e.path.join(' '))).sort((a,b)=>(a.helpSyntax||a.syntax).localeCompare(b.helpSyntax||b.syntax));
 const table=el('table'),head=el('tr');head.append(el('th','Command / syntax'),el('th','Description'));table.append(el('thead'));table.tHead.append(head);
 const tbody=el('tbody');for(const entry of rows){const row=el('tr'),cell=el('td');cell.append(el('code',entry.helpSyntax||entry.syntax));row.append(cell,el('td',entry.brief||entry.summary));tbody.append(row);}table.append(tbody);
 return [el('p',rows.length+' upstream commands from the console catalog. Commands MobMesh changes are under Enhanced+.'),table];
}
function toggleManual(){const panel=$('manual');panel.hidden=!panel.hidden;if(!panel.hidden)placeManual();if(panel.hidden||manualLoaded)return;manualLoaded=true;loadManual().catch(e=>{manualLoaded=false;$('manual-enhanced').textContent=e.message;});}
for(const tab of document.querySelectorAll?.('.manual-tabs [role=tab]')||[])tab.addEventListener('click',()=>{for(const other of document.querySelectorAll('.manual-tabs [role=tab]')){const on=other===tab;other.setAttribute('aria-selected',String(on));$(other.dataset.pane).hidden=!on;}$('manual-body').scrollTop=0;});
// connect's optional repeater: a full key (any repeater, temporary entry if needed) or a contact name; then the saved password logs in.
async function openTarget(spec){
 const key=/^[a-f0-9]{64}$/i.test(spec)?spec.toLowerCase():null;
 if(mode==='direct'){if(key&&key!==targetKey)add('status','Connected repeater is '+targetKey.slice(0,12)+', not '+key.slice(0,12)+'.');return;}
 if(key){$('manual-key').value=key;await connectKey();}
 else{
  const matches=contacts.filter(c=>c.type===2&&c.advName.toLowerCase()===spec.toLowerCase());
  if(matches.length!==1){add('status',matches.length?'Several repeaters are named "'+spec+'"; use its public key.':'No repeater contact named "'+spec+'".');showConnection();$('target').focus();return;}
  $('target').value=fullKey(matches[0].publicKey);auth=false;blocked=false;targetKey=$('target').value;$('password').value='';await offerSessions();
 }
 // With a saved password there is no dialog: the companion status line tracks the login instead.
 const saved=await credentials.read(targetKey);
 if(saved){const name=contacts.find(c=>fullKey(c.publicKey)===targetKey)?.advName||targetKey.slice(0,12);if(stepLine&&records.includes(stepLine)){stepLine.safeText=recordText('Logging in to '+name+' . . .');display();}await login(saved);}
 else{showConnection();$('password').focus();}
}
// Companion commands answer from the attached companion only; nothing goes over the radio.
const CONTACT_TYPES={1:'chat',2:'repeater',3:'room',4:'sensor'};
async function companionRun(name,arg){
 const c=transport.protocol;
 if(name==='info'){
  const self=transport.selfInfo||{},info=transport.deviceInfo||{};
  const battery=await c.rpc(12,()=>c.sendCommandGetBatteryVoltage()).then(b=>(b.batteryMilliVolts/1000).toFixed(2)+' V').catch(()=>'unavailable');
  return [['name',self.name],['model',info.manufacturerModel],['firmware',[info.firmwareVersion,info.firmware_build_date].filter(Boolean).join(' · ')],['battery',battery],
   ['radio',self.radioFreq?`${self.radioFreq/1000} MHz · BW ${self.radioBw/1000} kHz · SF${self.radioSf} · CR${self.radioCr}`:''],['tx power',self.txPower!==undefined?self.txPower+' dBm':''],
   ['public key',transport.gatewayKey],['contacts',String(contacts.length)]].filter(([,v])=>v).map(([k,v])=>k.padEnd(12)+v).join('\n');
 }
 if(name==='contacts'){
  await readContacts();const filter=arg.toLowerCase();
  const rows=contacts.filter(x=>!filter||x.advName.toLowerCase().includes(filter)).sort((a,b)=>a.advName.localeCompare(b.advName));
  if(!rows.length)return 'No contacts'+(filter?' matching "'+arg+'"':'')+'.';
  const width=Math.min(28,Math.max(...rows.map(x=>x.advName.length)))+2;
  return rows.map(x=>x.advName.slice(0,26).padEnd(width)+(CONTACT_TYPES[x.type]||'type '+x.type).padEnd(10)+routeText(x.outPathLen,x.outPath,contacts).replace(/^Route: /,'')).join('\n')+'\n'+rows.length+' contact'+(rows.length===1?'':'s');
 }
 const n=await readRadioTotals(true);
 return [['received',n.rx],['sent',n.tx],['sent flood',n.txFlood],['sent direct',n.txDirect],['recv flood',n.rxFlood],['recv direct',n.rxDirect]].map(([k,v])=>k.padEnd(12)+v).join('\n');
}
async function readDirectName(){try{const reply=directReply('get name',(await directCommand(transport,'get name')).text);return reply?.value.replace(/^>\s*/,'').trim()||null;}catch{return null;}}
async function open(reuse=null,quiet=false){if(busy||transport)return;busy=true;directName=null;writable=false;session=null;targetKey=null;if(reuse){keptHistory=history;add('boundary','── reconnecting ──');}else{records=[];history=new History();}auth=false;region=false;$('live').textContent='';mode=reuse?.mode||$('mode').value;linkTx=0;linkRx=0;$('tx-count').textContent='0 B';$('rx-count').textContent='0 B';$('radio-tx').textContent='—';$('radio-rx').textContent='—';temporaryTargets=new TemporaryTargets();const localEpoch=++epoch;status.update({epoch,linkState:'connecting',deviceConnected:false,lastTargetResponseAt:null});update();factory=transportFactory({text:live,frame:(direction,bytes)=>live(frameSummary(direction,bytes)),log:()=>{},activity,disconnected:()=>{if(localEpoch!==epoch)return;blocked=busy;auth=false;update();add('status','Physical device disconnected. Pending execution may be uncertain.'+(temporaryTargets.created.size?' Temporary companion entries may remain because cleanup could not run.':''));}});
  // The Connecting line is rewritten with the outcome rather than followed by it.
  let connecting=null,focusAfter=null;const settle=text=>{if(connecting&&records.includes(connecting)){connecting.safeText=recordText(text);display();}else add('status',text);};
  try{
    if(mode==='ble'){if(!navigator.bluetooth)throw Error('Web Bluetooth unavailable');const device=reuse?.device||await navigator.bluetooth.requestDevice({filters:[{services:[Constants.Ble.ServiceUuid.toLowerCase()]}]});lastLink={mode:'ble',device,targetKey:reuse?.targetKey||null};connecting=add('status',reuse?'Reconnecting . . .':'Connecting . . .');transport=await factory.ble(device);}else{if(!navigator.serial)throw Error('Web Serial unavailable');const port=reuse?.port||await navigator.serial.requestPort();lastLink={mode:'auto',port,targetKey:reuse?.targetKey||null};connecting=add('status',reuse?'Reconnecting . . .':'Connecting . . .');transport=await factory.serial(port,'auto');mode=transport.mode;}
    blocked=false;region=false;
    if(mode==='direct'){const key=await directCommand(transport,'get public.key');const m=key.text.match(/(?:^|\s)([a-f0-9]{64})(?=$|\s)/i);if(!m)throw Error('Full target public key was not returned');targetKey=m[1].toLowerCase();lastLink.targetKey=targetKey;directName=await readDirectName();auth=true;await offerSessions();settle(reuse?'Reconnected to repeater console.':'Connected to repeater console.');closeConnection();}
    else{const initialized=await initializeCompanion(transport.protocol,transport.deviceInfo);transport.gatewayKey=initialized.gatewayKey;transport.selfInfo=initialized.selfInfo;contacts=initialized.contacts;renderContacts();routeNote=null;routeStale=false;watchRoute(transport.protocol);await readRadioTotals().catch(e=>add('transport','Companion radio totals unavailable: '+e.message));startRadioPoll();if(reuse?.targetKey&&contacts.some(c=>c.type===2&&fullKey(c.publicKey)===reuse.targetKey))$('target').value=reuse.targetKey;targetKey=$('target').value||null;auth=false;$('remote').hidden=false;
      // Reconnect logs back in to the same repeater with its saved password; without one the dialog asks.
      const saved=reuse&&targetKey===reuse.targetKey?await credentials.read(targetKey):null;
      if(saved){settle('Reconnected to companion. Logging back in . . .');stepLine=connecting;await offerSessions();busy=false;await login(saved);}
      else{if(!quiet)showConnection();settle(reuse?'Reconnected to companion. Log in to continue.':'Connected to companion. Select a repeater and log in.');stepLine=connecting;if(targetKey)await offerSessions();else add('status','No repeater contacts available in this companion.');
      // The clicked transport button is now disabled, so focus would otherwise land nowhere; applied once controls re-enable.
      if(!quiet)focusAfter=targetKey?'password':'target';}}
  }catch(e){settle('Connection failed: '+e.message);await factory?.close();transport=null;targetKey=null;auth=false;}
  finally{busy=false;update();if(focusAfter)$(focusAfter).focus();}}
async function readContacts(){const c=transport.protocol,next=[];await c.rpc(4,()=>c.sendCommandGetContacts(),15000,x=>{if(next.length>=2048)throw Error('Contact limit exceeded');next.push(x);});contacts=next;renderContacts();}
function renderContacts(){const previous=targetKey;$('target').replaceChildren();for(const contact of contacts.filter(x=>x.type===2).sort((a,b)=>a.advName.localeCompare(b.advName))){const opt=document.createElement('option');opt.value=fullKey(contact.publicKey);opt.textContent=contact.advName+' · '+opt.value.slice(0,12);$('target').append(opt);}if(previous&&contacts.some(x=>x.type===2&&fullKey(x.publicKey)===previous))$('target').value=previous;}
async function refreshContacts(){if(busy||!transport?.protocol)return;busy=true;update();try{const previous=targetKey;await readContacts();targetKey=$('target').value||null;if(targetKey!==previous){auth=false;blocked=false;$('password').value='';if(targetKey)await offerSessions();else{await release();session=null;records=[];history=new History();display();}}add('status','Contact list refreshed from companion: '+contacts.filter(c=>c.type===2).length+' stored repeaters. This reads saved contacts; it does not add discovered adverts.');}finally{busy=false;update();}}
async function connectKey(){if(busy||!transport?.protocol)return;const value=$('manual-key').value;busy=true;update();try{await readContacts();const result=await temporaryTargets.ensure(value,contacts,transport.gatewayKey,transport.protocol);await readContacts();if(!contacts.some(c=>fullKey(c.publicKey)===result.key&&c.type===2))throw Error('Target entry was not returned by companion');const changed=targetKey!==result.key;targetKey=result.key;$('target').value=targetKey;if(changed){auth=false;blocked=false;tabState=null;await offerSessions();}else await restorePassword();add('status',result.temporary?'Public-key target selected. An internal temporary entry supports this connection.':'Public-key target selected using its existing contact.');$('manual-key').value='';}finally{busy=false;update();} $('password').focus();}
async function cleanupTemporary(){if(!temporaryTargets.created.size)return;if(!transport?.connected||transport.protocol?.closed){add('status','Temporary companion entries may remain; device disconnected before cleanup.');return;}try{await readContacts();await temporaryTargets.cleanup(transport.protocol,contacts,note=>add('status',note));}catch(e){add('status','Temporary entries may remain; cleanup unavailable: '+e.message);}}
async function close(){stopRadioPoll();epoch++;abort?.abort();for(const cancel of [...transport?.protocol?.pending||[]])cancel();await cleanupTemporary();await factory?.close();transport=null;auth=false;blocked=false;region=false;busy=false;$('remote').hidden=true;history.reset();add('boundary','Disconnected.');await release();update();}
async function login(providedPassword=null){
 if(busy||!transport?.protocol||!targetKey)return;
 let password=typeof providedPassword==='string'?providedPassword:$('password').value;$('password').value='';
 const key=targetKey,c=transport.protocol,notes=[];busy=true;auth=false;update();
 try{
  const selected=contacts.find(contact=>contact.type===2&&fullKey(contact.publicKey)===key);
  if(!selected)throw Error('Selected repeater is not in the current companion snapshot');
  if(contacts.filter(contact=>fullKey(contact.publicKey).startsWith(key.slice(0,12))).length!==1)throw Error('Six-byte contact prefix collision');
  const path=selected.outPathLen&255;
  add('transport','Diagnostic engine login · target '+key.slice(0,12)+' · stored route '+path+' · '+(path===255?'flood':path===0?'direct zero-hop':'direct stored path'));
  const remembered=password,remember=$('remember-password').checked!==false;const result=await authenticateRepeater(c,key,password,note=>{notes.push(note);$('assistance').textContent=note;});password='';if(remember){const saved=await credentials.save(key,remembered);$('credential-status').textContent=saved?'Password saved for this repeater.':'Password could not be saved · '+credentials.error;}else await credentials.forget(key);
  for(const note of notes)add('transport',note);
  auth=true;blocked=false;if(lastLink)lastLink.targetKey=key;if(result.login?.gatewaySent?.viaFlood)routeStale=true;if(!session)await chooseSession();$('assistance').textContent='';
  // The companion's next-step line is rewritten once login completes.
  if(stepLine&&records.includes(stepLine)){stepLine.safeText='Connected · repeater admin access verified.';stepLine=null;display();}else add('status','Connected · repeater admin access verified.');status.update({lastTargetResponseAt:Date.now()});closeConnection();
 }catch(e){for(const note of notes)add('transport',note);add('status','Login stopped: '+e.message);$('assistance').textContent='Login stopped: '+e.message;auth=false;blocked=true;await factory?.close();transport=null;$('remote').hidden=true;}
 finally{password='';busy=false;update();refreshRoute();}
}
function clearChoices(){$('completion-choices').textContent='';$('completion-choices').hidden=true;}
function clearTerminal(){records=[];display();$('command').focus();}
async function submit(){clearChoices();tabState=null;const raw=$('command').value;const help=companionCommand(raw)?null:helpRequest(raw,catalog);if(help!==null){add('command',raw,help.trim()&&!localCommand(help)?sensitive(raw,catalog):false);if(help.trim())add('local-help',helpText(help,catalog,mode));else add('help-overview',helpOverview());$('command').value='';tabState=null;return;}// A blank Enter echoes an empty prompt line, as a shell does; nothing is sent.
  if(!raw.trim()&&!region){add('command','');$('command').value='';resizePrompt();return;}
  const local=localCommand(raw);
  if(local){
    if(!local.valid){add('status','Usage: '+(local.name==='connect'?'connect [usb|bluetooth] ["contact name"|public_key]':local.name));return;}
    $('command').value='';resizePrompt();history.add(local.name);
    if(local.name==='background'){if(local.background){backgroundOn=local.background==='on';setBackground(backgroundOn);try{localStorage.setItem('console-background',local.background);}catch{}}add('status','Background: '+(backgroundOn?'on':'off'));return;}
    if(local.name==='theme'){if(local.theme){applyTheme(local.theme);try{localStorage.setItem('console-theme',local.theme);}catch{}}add('status','Theme: '+currentTheme()+' · available: '+THEMES.join(', '));return;}
    if(local.name==='clear')clearTerminal();
    else if(local.name==='exit')await close();
    else if(local.name==='reconnect'){if(transport||busy){add('status','Already connected.');return;}if(!lastLink){add('status','Nothing to reconnect. Use connect first.');return;}await open(lastLink);}
    else if(local.transport){if(transport||busy){add('status','Already connected. Type exit first.');return;}selectTransport(local.transport);await open(null,!!local.target);if(local.target&&transport?.connected)await openTarget(local.target);}
    else showConnection();
    return;
  }
  const comp=companionCommand(raw);
  if(comp){
   if(!transport?.protocol){add('status','Companion commands need a companion connection.');return;}
   add('command',raw);history.add(raw);$('command').value='';resizePrompt();
   if(!comp.name||comp.name==='?'||comp.arg==='?'){add('local-help',Object.entries(COMPANION_COMMANDS).map(([n,d])=>'/'+n+'\n'+d).join('\n\n'));return;}
   if(!Object.hasOwn(COMPANION_COMMANDS,comp.name)){add('status','Unknown companion command: '+comp.name+'. Try / ? for the list.');return;}
   if(busy){$('assistance').textContent='One command is pending; no command has been queued.';return;}
   busy=true;update();try{add('reply',await companionRun(comp.name,comp.arg));}catch(e){add('status','Companion command failed: '+e.message);}finally{busy=false;update();$('command').focus();}
   return;
  }
  if(raw.startsWith(':')&&!raw.startsWith(':raw ')){add('status','Unknown local command. Use :raw followed by an exact firmware command.');return;}
  if(busy){$('assistance').textContent='One command is pending; no command has been queued.';return;}
  if(!transport?.connected||!session||!writable||!auth||(blocked&&!region)){if(blocked&&transport?.connected&&mode!=='direct'){showConnection();$('password').focus();$('assistance').textContent='Reauthenticate to continue';}else{showConnection();$('assistance').textContent='Connect and authenticate before sending';}return;}
  let text;try{text=wireInput(raw,mode);}catch(e){add('status',e.message);return;}
  const entry=catalog.resolve(raw);if(mode!=='direct'&&entry?.remote===false){add('status','This command is only available over direct serial.');return;}
  const secret=sensitive(raw,catalog);$('assistance').textContent='Sending command…';busy=true;abort=new AbortController();update();add('command',raw,secret);if(!secret)history.add(raw);$('command').value='';
  // Grey placeholder under the command; the reply replaces it, a failure removes it.
  const pending=add('pending','...');
  try{
    let reply;
    if(mode==='direct'){const policy=region||text==='region load'?'interactive':text==='log'?'eof':text==='get acl'?'stream':/^(?:reboot|clkreboot|poweroff|shutdown)(?: |$)/.test(text)?'no-reply':'marked';reply=await directCommand(transport,text,{kind:policy,signal:abort.signal});if(text==='region load'&&!/ERR|Unknown|refus/i.test(reply.text))region=true;else if(region&&text==='')region=false;if(policy==='no-reply'&&!/ERR|refus/i.test(reply.text)){auth=false;blocked=true;}}
    else{const probe=new RemoteProbe(transport.protocol,deferredObserver(note=>{add('transport',note);$('assistance').textContent=note.startsWith('Companion SENT')?'Waiting for repeater…':'Sending command…';}));reply=await probe.cli(targetKey,text);}
    Object.assign(pending,{kind:'reply',safeText:recordText(mode==='direct'?displayReply(text,reply.text):reply.text,secret),redacted:secret});display();add('transport','Reply attribution: '+reply.completionBasis+(Number.isInteger(reply.pathLength)?' · reply came back '+(reply.pathLength===255?'direct':(reply.pathLength&63)+' hop'+((reply.pathLength&63)===1?'':'s')):''));$('assistance').textContent='';status.update({lastTargetResponseAt:Date.now()});
  }catch(e){add('transport','Command stopped: '+e.message);if(e instanceof RemoteReplyTimeout&&transport?.connected&&auth){add('status','No reply received. You can send another command.');$('assistance').textContent='Ready · previous command received no reply';}else{blocked=true;add('status','Command stopped. Open diagnostics for details.');$('assistance').textContent='Command stopped · check diagnostics';}}
  finally{if(pending.kind==='pending'){records=records.filter(r=>r!==pending);display();}busy=false;abort=null;update();refreshRoute();$('command').focus();}}
// Root command names render as a two-column list; every other match list is one line split by middle dots.
function showChoices(result){
 const box=$('completion-choices');
 if(result.rows?.length){box.replaceChildren(...result.rows.map(([name,options])=>{const row=document.createElement('div');row.className='choice-row';const command=document.createElement('span');command.className='choice-command';command.textContent=name;const rest=document.createElement('span');rest.className='choice-options';rest.textContent=options;row.append(command,rest);return row;}));return;}
 box.textContent=result.values.join(' · ')||result.hint||'No known completion.';
}
function errorAction(fn){return ()=>Promise.resolve().then(fn).catch(e=>{add('status',e.message);busy=false;update();});}
$('reauthenticate').onclick=()=>{showConnection();$('password').focus();};
function selectTransport(value){if(transport||busy)return;$('mode').value=value;$('mode-usb').setAttribute?.('aria-pressed',String(value==='auto'));$('mode-bluetooth').setAttribute?.('aria-pressed',String(value==='ble'));}
// Choosing a transport is the connect action; there is no separate Connect button.
// Before connecting, Tab and Shift+Tab only alternate between the two transport choices.
$('connection-dialog').addEventListener?.('keydown',e=>{if(e.key!=='Tab'||transport||busy)return;e.preventDefault();$(document.activeElement?.id==='mode-usb'?'mode-bluetooth':'mode-usb').focus();});
$('mode-usb').onclick=errorAction(()=>{selectTransport('auto');return open();});$('mode-bluetooth').onclick=errorAction(()=>{selectTransport('ble');return open();});
$('connection-panel').onclick=showConnection;$('connection-close').onclick=closeConnection;
$('connection-dialog').addEventListener?.('click',e=>{if(e.target===$('connection-dialog')){const bounds=e.target.getBoundingClientRect();if(e.clientX<bounds.left||e.clientX>bounds.right||e.clientY<bounds.top||e.clientY>bounds.bottom)closeConnection();}});
$('diagnostics-toggle').onclick=()=>{$('diagnostics').hidden=!$('diagnostics').hidden;};$('diagnostics-close').onclick=()=>{$('diagnostics').hidden=true;};$('manual-close').onclick=()=>{$('manual').hidden=true;};
$('clear-terminal').onclick=clearTerminal;
$('fullscreen').onclick=()=>{const action=document.fullscreenElement?document.exitFullscreen?.():document.documentElement?.requestFullscreen?.();Promise.resolve(action).catch(()=>{$('assistance').textContent='Full screen is unavailable in this browser.';});};
$('terminal-scroll').onclick=e=>{if(!globalThis.getSelection?.()?.toString()&&['terminal-scroll','output','welcome'].includes(e.target.id))$('command').focus();};
document.addEventListener?.('keydown',e=>{if(e.isComposing)return;if(e.key==='F2'){e.preventDefault();if($('connection-dialog').open)closeConnection();else showConnection();}else if(e.key==='F3'){e.preventDefault();$('diagnostics-toggle').click();}else if(e.key==='F1'){e.preventDefault();toggleManual();}else if(e.ctrlKey&&e.shiftKey&&e.key==='Enter'){e.preventDefault();$('fullscreen').click();}});
// Floating panels move by their header and resize from a bottom-left grip.
function floatingPanel(panelId,headId,gripId){
 let drag=null,size=null;const panel=$(panelId),head=$(headId),grip=$(gripId);
 head.addEventListener?.('pointerdown',e=>{if(e.target.closest('button'))return;const rect=panel.getBoundingClientRect();drag={pointer:e.pointerId,x:e.clientX,y:e.clientY,left:rect.left,top:rect.top};panel.dataset.placed='user';e.currentTarget.setPointerCapture(e.pointerId);e.preventDefault();});
 head.addEventListener?.('pointermove',e=>{if(!drag||drag.pointer!==e.pointerId)return;panel.style.right='auto';panel.style.bottom='auto';panel.style.left=Math.max(8,Math.min(window.innerWidth-panel.offsetWidth-8,drag.left+e.clientX-drag.x))+'px';panel.style.top=Math.max(8,Math.min(window.innerHeight-70,drag.top+e.clientY-drag.y))+'px';});
 grip.addEventListener?.('pointerdown',e=>{panel.dataset.placed='user';size={pointer:e.pointerId,x:e.clientX,y:e.clientY,rect:panel.getBoundingClientRect()};e.currentTarget.setPointerCapture(e.pointerId);e.preventDefault();});
 grip.addEventListener?.('pointermove',e=>{if(!size||size.pointer!==e.pointerId)return;const {rect}=size,width=Math.max(320,Math.min(rect.right-8,rect.width+size.x-e.clientX)),height=Math.max(240,Math.min(window.innerHeight-rect.top-8,rect.height+e.clientY-size.y));panel.style.right='auto';panel.style.bottom='auto';panel.style.top=rect.top+'px';panel.style.left=(rect.right-width)+'px';panel.style.width=width+'px';panel.style.height=height+'px';});
 for(const end of ['pointerup','pointercancel']){head.addEventListener?.(end,()=>{drag=null;});grip.addEventListener?.(end,()=>{size=null;});}
}
floatingPanel('diagnostics','diagnostics-head','diagnostics-resize');floatingPanel('manual','manual-head','manual-resize');
$('password').oninput=()=>{passwordRevision++;};$('password').onkeydown=e=>{if(e.key==='Enter'){e.preventDefault();login().catch(error=>add('status',error.message));}};
$('forget-password').onclick=errorAction(async()=>{if(!targetKey)return;passwordRevision++;const forgotten=await credentials.forget(targetKey);$('password').value='';$('credential-status').textContent=forgotten?'Saved password removed.':'Password could not be removed · '+credentials.error;});
$('login').onclick=errorAction(login);$('refresh-contacts').onclick=errorAction(refreshContacts);$('connect-key').onclick=errorAction(connectKey);$('target').onchange=errorAction(async()=>{auth=false;blocked=false;targetKey=$('target').value;tabState=null;$('password').value='';await offerSessions();});
// Radio TX/RX are the companion's own packet totals (command 56, subtype 2), not the remote repeater's.
async function readRadioTotals(quiet=false){const probe=new RemoteProbe(transport.protocol);const bytes=await probe.wait([24],()=>transport.protocol.sendToRadioFrame(Uint8Array.of(56,2)),5000,f=>f[1]===2);const counts=packetCounters(bytes);$('radio-tx').textContent=String(counts.tx);$('radio-rx').textContent=String(counts.rx);if(!quiet)add('transport','Companion radio totals · TX '+counts.tx+' · RX '+counts.rx+' · direct TX '+counts.txDirect+' · flood TX '+counts.txFlood+' (all traffic; compare before/after).');return counts;}
// While a companion is connected the totals refresh every 10 s; a tick is skipped while a command is in flight.
let radioPoll=null;
function startRadioPoll(){stopRadioPoll();radioPoll=setInterval(()=>{if(transport?.connected&&transport.protocol&&!busy){readRadioTotals(true).catch(()=>{});refreshRoute();}},10000);radioPoll.unref?.();}
function stopRadioPoll(){clearInterval(radioPoll);radioPoll=null;}
if($('packet-stats'))$('packet-stats').onclick=errorAction(async()=>{if(busy||!transport?.protocol)throw Error('Connect a companion and wait for the current command first');busy=true;update();try{await readRadioTotals();}finally{busy=false;update();}});
$('radio-refresh').onclick=()=>$('packet-stats').click();
$('command-form').onsubmit=e=>{e.preventDefault();submit().catch(e=>add('status',e.message));};
$('cancel').onclick=()=>{if(!busy){$('command').value='';return;}abort?.abort();for(const cancel of [...transport?.protocol?.pending||[]])cancel();blocked=true;add('status','Local wait cancelled; no firmware cancellation sent.');update();};
$('command').onfocus=()=>escapeTab=false;$('command').oninput=()=>{tabState=null;clearChoices();resizePrompt();};
$('command').onkeydown=e=>{if(e.isComposing)return;const input=$('command'),text=input.value,cursor=input.selectionStart;
  if(e.key==='Escape'){escapeTab=true;tabState=null;clearChoices();$('assistance').textContent='Tab will leave the editor.';return;}
  if(e.ctrlKey&&e.key.toLowerCase()==='l'){e.preventDefault();$('clear-terminal').click();return;}
  if(e.ctrlKey&&e.key.toLowerCase()==='c'&&!input.value.substring(input.selectionStart,input.selectionEnd)){e.preventDefault();if(busy)$('cancel').click();else input.value='';return;}
  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();submit().catch(err=>add('status',err.message));return;}
  if(e.key==='ArrowUp'||e.key==='ArrowDown'){if(region)return;e.preventDefault();const value=history.move(e.key==='ArrowUp'?-1:1,text,cursor);input.value=value.text;input.setSelectionRange(value.cursor,value.cursor);tabState=null;clearChoices();resizePrompt();return;}
  if(e.key==='Tab'&&!escapeTab){e.preventDefault();if(region){$('assistance').textContent='Region row mode: preserve indentation; blank Enter commits. No command completion.';return;}const onCompanion=!!transport?.protocol,result=onCompanion&&companionCommand(text)?companionComplete(text,cursor):auth?complete(catalog,text,cursor):localComplete(text,cursor,{companion:onCompanion}),signature=text+'@'+cursor;if(tabState===signature){showChoices(result);$('completion-choices').hidden=false;$('assistance').textContent=result.values.length?result.values.length+' matches · keep typing to narrow':'No known completion.';}else{if(result.replacement){const r=result.replacement;input.value=text.slice(0,r.start)+r.text+text.slice(r.end);input.setSelectionRange(r.cursor,r.cursor);}$('assistance').textContent=result.hint||'';}tabState=input.value+'@'+input.selectionStart;resizePrompt();return;}
  if(['ArrowLeft','ArrowRight','Home','End'].includes(e.key))tabState=null;
};
window.addEventListener('pagehide',()=>{factory?.close();});
try{catalog=await loadCatalog();$('catalog-status').textContent=`${catalog.entries.length} effective signatures from upstream and all four CLI-additions docs. Full parameter verification gate: pending.`;update();$('command').focus();}catch(e){$('status').textContent='Console initialization failed: '+e.message;}
if(document.modelContext?.registerTool){const lifecycle=new AbortController();try{Promise.resolve(document.modelContext.registerTool({name:'read_meshcore_console_status',description:'Read connection status without querying hardware.',inputSchema:{type:'object',properties:{},additionalProperties:false},annotations:{readOnlyHint:true,untrustedContentHint:true},execute(input){if(!input||Object.keys(input).length)throw Error('Expected empty object');return {...status.getSnapshot(),catalogEntries:catalog?.entries.length,regionMode:region};}},{signal:lifecycle.signal})).catch(()=>{});}catch{}window.addEventListener('pagehide',()=>lifecycle.abort(),{once:true});}
