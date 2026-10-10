export const LOCAL_COMMANDS=Object.freeze({
  connect:'Open the connection dialog, or connect directly: connect usb|bluetooth ["contact name"|public_key].',
  exit:'Disconnect the console. Aliases: quit, disconnect, close.',
  reconnect:'Reopen the last device without a chooser, keep the transcript, and log back in to the same repeater.',
  clear:'Clear the console output. Alias: cls.',
  background:'Show or switch the animated mesh background: background on | off.',
  theme:'Show or switch the colour theme: theme default | amber | grey | green | retro.',
});

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
    return target===undefined?{name,valid:true,transport:CONNECT_TRANSPORTS[m[1]]}:{name,valid:true,transport:CONNECT_TRANSPORTS[m[1]],target};
  }
  if(name==='background'&&args.length===1)return ['on','off'].includes(args[0])?{name,valid:true,background:args[0]}:{name,valid:false};
  if(name==='theme'&&args.length===1)return THEMES.includes(args[0])?{name,valid:true,theme:args[0]}:{name,valid:false};
  return {name,valid:args.length===0};
}

// Completion at the console root, before a repeater console is open.
export function localComplete(text,cursor,{companion=false}={}){
  const before=text.slice(0,cursor),words=before.split(/\s+/),prefix=words.at(-1),start=cursor-prefix.length;
  let values;
  // Aliases complete from a typed prefix but stay out of the bare list.
  if(words.length===1)values=[...Object.keys(LOCAL_COMMANDS),...(prefix?Object.keys(LOCAL_ALIASES):[])].filter(n=>n.startsWith(prefix));
  else if(words.length===2&&words[0]==='connect')values=Object.keys(CONNECT_TRANSPORTS).filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='background')values=['on','off'].filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='theme')values=THEMES.filter(t=>t.startsWith(prefix));
  else values=[];
  // Command names list as rows of name and options; argument lists stay flat.
  const options={connect:Object.keys(CONNECT_TRANSPORTS),background:['on','off'],theme:THEMES};
  const rows=words.length===1?values.map(v=>[v,(options[v]||[]).join(' | ')]):null;
  if(rows&&companion&&!prefix)rows.push(['/ companion',Object.keys(COMPANION_COMMANDS).join(' | ')]);
  let replacement=null;
  if(values.length===1){const text=values[0]+(words.length===1&&['connect','theme','background'].includes(values[0])?' ':'');replacement={start,end:cursor,text,cursor:start+text.length};}
  return {values,rows,hint:'',replacement};
}

// Companion commands run on the attached companion itself, reached with "/info" (as meshcore-cli does) or "companion info".
export const COMPANION_COMMANDS=Object.freeze({
  info:'Companion name, model, firmware, battery and radio settings.',
  contacts:'Companion contacts with type and stored route; optional name filter.',
  stats:'Companion packet counters: received, sent, flood and direct.',
});
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
  if(!(slash?words.length===1:words.length===2))return {values:[],hint:'',replacement:null};
  const values=Object.keys(COMPANION_COMMANDS).filter(n=>n.startsWith(prefix)),start=cursor-prefix.length;
  const replacement=values.length===1?{start,end:cursor,text:values[0]+' ',cursor:start+values[0].length+1}:null;
  return {values,hint:'',replacement};
}
