export const LOCAL_COMMANDS=Object.freeze({
  connect:'Open the connection dialog, or connect directly: connect usb|bluetooth [companion | "repeater name" | public_key]. companion stays on the companion; login reaches a repeater from there.',
  exit:'Disconnect the console. Aliases: quit, disconnect, close.',
  reconnect:'Reopen the last device without a chooser, keep the transcript, and log back in to the same repeater.',
  login:'Log in to a repeater through the connected companion: login "name" | public_key. A key outside the contact list gets a temporary entry. Asks for the password if none is saved.',
  logout:'Log out of the repeater and return to the companion.',
  clear:'Clear the console output. Alias: cls.',
  background:'Show or switch the animated mesh background: background on | off.',
  theme:'Show or switch the colour theme: theme default | amber | grey | green | retro.',
});

const COMPANION_ONLY=['login','logout'];
export const LOCAL_ALIASES=Object.freeze({quit:'exit',disconnect:'exit',close:'exit',cls:'clear'});
export const THEMES=Object.freeze(['default','amber','grey','green','retro']);
export const CONNECT_TRANSPORTS=Object.freeze({usb:'auto',bluetooth:'ble'});

export function localCommand(raw){
  const text=raw.trim(),[typed,...args]=text.split(/\s+/),name=LOCAL_ALIASES[typed]||typed;
  if(!Object.hasOwn(LOCAL_COMMANDS,name))return null;
  if(name==='connect'&&args.length){
    // Optional repeater after the transport: a full public key, a bare name, or a name in double or single quotes.
    const m=text.match(/^\S+\s+(\S+)(?:\s+(?:"([^"]+)"|'([^']+)'|(\S+)))?\s*$/);
    if(!m||!Object.hasOwn(CONNECT_TRANSPORTS,m[1]))return {name,valid:false};
    const target=m[2]??m[3]??m[4];
    if(target===undefined)return {name,valid:true,transport:CONNECT_TRANSPORTS[m[1]]};
    // Unquoted "companion" means the companion itself; a repeater with that name needs quotes or its key.
    if(m[4]==='companion')return {name,valid:true,transport:CONNECT_TRANSPORTS[m[1]],companionOnly:true};
    return {name,valid:true,transport:CONNECT_TRANSPORTS[m[1]],target};
  }
  if(name==='login'){const m=text.match(/^\S+\s+(?:"([^"]+)"|'([^']+)'|(\S+))\s*$/);return m?{name,valid:true,target:m[1]??m[2]??m[3]}:{name,valid:false};}
  if(name==='background'&&args.length===1)return ['on','off'].includes(args[0])?{name,valid:true,background:args[0]}:{name,valid:false};
  if(name==='theme'&&args.length===1)return THEMES.includes(args[0])?{name,valid:true,theme:args[0]}:{name,valid:false};
  return {name,valid:args.length===0};
}

// Completion at the console root, before a repeater console is open.
export function localComplete(text,cursor,{companion=false}={}){
  const before=text.slice(0,cursor),words=before.split(/\s+/),prefix=words.at(-1),start=cursor-prefix.length;
  let values;
  // Aliases complete from a typed prefix but stay out of the bare list.
  // login and logout only mean something once a companion is connected.
  if(words.length===1)values=[...Object.keys(LOCAL_COMMANDS),...(prefix?Object.keys(LOCAL_ALIASES):[])].filter(n=>n.startsWith(prefix)&&(companion||!COMPANION_ONLY.includes(n)));
  else if(words.length===2&&words[0]==='connect')values=Object.keys(CONNECT_TRANSPORTS).filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='background')values=['on','off'].filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='theme')values=THEMES.filter(t=>t.startsWith(prefix));
  else values=[];
  // Command names list as rows of name and options; argument lists stay flat.
  // Usage in the familiar man-page form: {choose one} [optional].
  const usage={connect:'{usb|bluetooth} [companion|"repeater"|key]',login:'{"repeater"|key}',background:'{on|off}',theme:'{'+THEMES.join('|')+'}'};
  const rows=words.length===1?values.map(v=>[v,usage[v]||'']):null;
  if(rows&&companion&&!prefix)rows.push(['/','<companion command>   (/? lists them)']);
  let replacement=null;
  if(values.length===1){const text=values[0]+(words.length===1&&['connect','theme','background'].includes(values[0])?' ':'');replacement={start,end:cursor,text,cursor:start+text.length};}
  return {values,rows,hint:'',replacement};
}

// Companion commands run on the attached companion itself, reached with "/info" (as meshcore-cli does) or "companion info".
export const COMPANION_COMMANDS=Object.freeze({
  info:'Companion name, model, firmware, battery and radio settings.',
  ver:'Companion firmware version and build date.',
  board:'Companion hardware model.',
  clock:'Companion clock, and its offset from this computer.',
  get:'Read a companion setting, named as on a repeater.',
  stats:'Companion counters; all three groups when none is given.',
  contacts:'Companion contacts with type and stored route; optional name filter. Alias: list.',
  list:'Alias of contacts.',
  set:'Change a companion setting, named as on a repeater. radio, freq and prv.key ask for confirmation.',
  time:'Set the companion clock to a Unix time; it refuses to move backwards.',
  advert:'Send the companion advert as a flood.',
  'advert.zerohop':'Send the companion advert to direct neighbours only.',
  reset:'reset path <contact>: forget the stored route so the next message floods.',
  card:'Print the companion contact card (meshcore:// URI) to share.',
  export:'Print a contact card (meshcore:// URI) for a contact, or the companion itself when none is given.',
  import:'Add a contact from a meshcore:// card.',
  reboot:'Restart the companion. Asks for confirmation.',
  erase:'Factory reset: wipes contacts, keys and settings. Asks for confirmation.',
});
// Repeater commands a companion has no protocol for; they answer plainly instead of failing silently.
export const COMPANION_UNAVAILABLE=Object.freeze(['poweroff','shutdown','clkreboot','start','tempradio','password','log','neighbors','clear','powersaving','region']);
// Syntax shown by "?" help and the F1 Companion tab; commands without an entry take no arguments.
export const COMPANION_SYNTAX=Object.freeze({set:'/set <name | lat | lon | tx | radio | freq | af | rxdelay | prv.key> <value>',time:'/time <epoch>',reset:'/reset path <contact>',import:'/import <meshcore://card>',export:'/export [contact]',clock:'/clock [sync]',get:'/get <name | radio | freq | tx | lat | lon | public.key | af | rxdelay | prv.key>',stats:'/stats [core | radio | packets]',contacts:'/contacts [filter]',list:'/list [filter]'});
export const companionSyntax=name=>COMPANION_SYNTAX[name]||'/'+name;
// Arguments the companion commands complete; names mirror the repeater CLI.
export const COMPANION_ARGS=Object.freeze({get:['name','radio','freq','tx','lat','lon','public.key','af','rxdelay','prv.key'],set:['name','lat','lon','tx','radio','freq','af','rxdelay','prv.key'],stats:['core','radio','packets'],clock:['sync'],reset:['path']});
export function companionCommand(raw){
  const text=raw.trim();let rest;
  if(text.startsWith('/'))rest=text.slice(1);
  else{const [word,...more]=text.split(/\s+/);if(word!=='companion')return null;rest=more.join(' ');}
  const [name='',...args]=rest.trim().split(/\s+/).filter(Boolean);
  return {name,arg:args.join(' ')};
}
export function companionComplete(text,cursor){
  const before=text.slice(0,cursor),slash=before.startsWith('/');
  const words=before.split(/\s+/),prefix=slash&&words.length===1?before.slice(1):words.at(-1);
  const depth=slash?words.length:words.length-1;
  if(depth===2){
    const options=COMPANION_ARGS[slash?words[0].slice(1):words[1]]||[],values=options.filter(o=>o.startsWith(words.at(-1))),start=cursor-words.at(-1).length;
    return {values,hint:'',replacement:values.length===1?{start,end:cursor,text:values[0],cursor:start+values[0].length}:null};
  }
  if(depth!==1)return {values:[],hint:'',replacement:null};
  const values=Object.keys(COMPANION_COMMANDS).filter(n=>n.startsWith(prefix)),start=cursor-prefix.length;
  const replacement=values.length===1?{start,end:cursor,text:values[0]+(COMPANION_ARGS[values[0]]?' ':''),cursor:start+values[0].length+(COMPANION_ARGS[values[0]]?1:0)}:null;
  return {values,hint:'',replacement};
}
