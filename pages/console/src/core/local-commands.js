export const LOCAL_COMMANDS=Object.freeze({
  connect:'Open the connection dialog, or connect directly: connect usb | connect bluetooth.',
  exit:'Disconnect the console. Aliases: quit, disconnect, close.',
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
  if(name==='connect'&&args.length===1&&Object.hasOwn(CONNECT_TRANSPORTS,args[0]))return {name,valid:true,transport:CONNECT_TRANSPORTS[args[0]]};
  if(name==='background'&&args.length===1)return ['on','off'].includes(args[0])?{name,valid:true,background:args[0]}:{name,valid:false};
  if(name==='theme'&&args.length===1)return THEMES.includes(args[0])?{name,valid:true,theme:args[0]}:{name,valid:false};
  return {name,valid:args.length===0};
}

// Completion at the console root, before a repeater console is open.
export function localComplete(text,cursor){
  const before=text.slice(0,cursor),words=before.split(/\s+/),prefix=words.at(-1),start=cursor-prefix.length;
  let values;
  // Aliases complete from a typed prefix but stay out of the bare list.
  if(words.length===1)values=[...Object.keys(LOCAL_COMMANDS),...(prefix?Object.keys(LOCAL_ALIASES):[])].filter(n=>n.startsWith(prefix));
  else if(words.length===2&&words[0]==='connect')values=Object.keys(CONNECT_TRANSPORTS).filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='background')values=['on','off'].filter(t=>t.startsWith(prefix));
  else if(words.length===2&&words[0]==='theme')values=THEMES.filter(t=>t.startsWith(prefix));
  else values=[];
  const labels=values.map(v=>words.length>1?v:v==='connect'?'connect [usb, bluetooth]':v==='theme'?'theme ['+THEMES.join(', ')+']':v==='background'?'background [on, off]':v);
  let replacement=null;
  if(values.length===1){const text=values[0]+(words.length===1&&['connect','theme','background'].includes(values[0])?' ':'');replacement={start,end:cursor,text,cursor:start+text.length};}
  return {values,labels,hint:'',replacement};
}
