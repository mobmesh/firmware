export const MAX_FRAME = 176;
export const DIRECT_COMMANDS = ['ver','board','get public.key'];
export function directBytes(command) {
  if (!DIRECT_COMMANDS.includes(command)) throw Error('Query is not on the diagnostic allowlist');
  return new TextEncoder().encode(command+'\r');
}
export function usbFrame(payload) {
  if (!payload.length || payload.length>MAX_FRAME) throw Error('Invalid application frame length');
  return Uint8Array.from([0x3c,payload.length&255,payload.length>>8,...payload]);
}
export class UsbFrames {
  constructor(onFrame,onWarning=()=>{}){this.buffer=[];this.onFrame=onFrame;this.onWarning=onWarning;this.startedAt=0;}
  feed(bytes,now=Date.now()) {
    if(this.buffer.length && now-this.startedAt>5000){this.buffer=[];this.onWarning('Incomplete frame expired');}
    if(!this.buffer.length)this.startedAt=now;
    this.buffer.push(...bytes);
    while(this.buffer.length>=3){
      const n=this.buffer[1]+256*this.buffer[2];
      if(this.buffer[0]!==0x3e || !n || n>MAX_FRAME){this.buffer.shift();continue;}
      if(this.buffer.length<n+3)break;
      const frame=Uint8Array.from(this.buffer.splice(0,n+3).slice(3));this.onFrame(frame);this.startedAt=now;
    }
  }
}
// Payloads that can carry passwords, message text or the BLE PIN show code and length only.
const OPAQUE_FRAMES=new Set([2,7,8,13,16,26,0x85]);
export function frameSummary(direction,bytes){const code=bytes[0]??0,hex=OPAQUE_FRAMES.has(code)?'(payload hidden)':Array.from(bytes.slice(1,33),b=>b.toString(16).padStart(2,'0')).join(' ')+(bytes.length>33?' …':'');return `${direction} 0x${code.toString(16).padStart(2,'0')} ${bytes.length} B ${hex}\n`;}
export function fullKey(bytes){return Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');}
export function deviceInfo(bytes){
  if(bytes[0]!==13 || bytes.length<20)throw Error('Truncated DeviceInfo');
  const str=(start,end)=>new TextDecoder().decode(bytes.slice(start,end)).split('\0')[0];
  return {firmwareVer:bytes[1],maxContacts:bytes[2]*2,maxGroupChannels:bytes[3],
    firmware_build_date:str(8,20),manufacturerModel:str(20,60),
    firmwareVersion:bytes.length>=80?str(60,80):null,
    repeatEnabled:bytes.length>80?!!bytes[80]:null,pathHashMode:bytes.length>81?bytes[81]:null};
  // bytes 4..7 are the BLE PIN. Never display or export them.
}
export function directReply(command,text) {
  const marker=text.indexOf('  -> ');
  if(marker<0)return null;
  const value=text.slice(marker+5).trim();
  if(!value)return null;
  if(command==='get public.key'){
    const key=value.match(/(?:^|\s)([a-f0-9]{64})(?=$|\s)/i);
    if(!key)throw Error('Reply did not contain a full 32-byte public key');
    return {value,publicKey:key[1].toLowerCase()};
  }
  return {value};
}
