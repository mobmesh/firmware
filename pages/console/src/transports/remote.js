import {DIRECT_COMMANDS,fullKey} from '../protocol.js';
export const TARGET_KEY='c0ad2d3f4a49d2c38cbb918cb2f8a5acb82d10e4a372e3a34bd98db01917a9d0';
const tokens=new Set();
export function token(){for(let i=0;i<1296;i++){const t=i.toString(36).padStart(2,'0').toUpperCase();if(!tokens.has(t)){tokens.add(t);return t;}}throw Error('Reply tags exhausted; reload only after outstanding traffic settles');}
export function keyBytes(key){if(!/^[a-f0-9]{64}$/i.test(key))throw Error('Invalid full public key');return Uint8Array.from(key.match(/../g),b=>parseInt(b,16));}
export function message(frame){
  const code=frame[0],offset=code===16?4:1;
  if(![7,16].includes(code)||frame.length<offset+12)return null;
  return {senderPrefix:fullKey(frame.slice(offset,offset+6)),pathLength:frame[offset+6],textType:frame[offset+7],senderTimestamp:new DataView(frame.buffer,frame.byteOffset+offset+8,4).getUint32(0,true),text:new TextDecoder().decode(frame.slice(offset+12)).replace(/\0+$/,'')};
}
export function matchingReply(frame,key,tag){const m=message(frame);return m&&m.senderPrefix===key.slice(0,12)&&m.textType===1&&m.text.startsWith(tag+'|')?{...m,text:m.text.slice(3)}:null;}
export function replyWaitMs(estimatedTimeoutMs){if(!Number.isFinite(estimatedTimeoutMs)||estimatedTimeoutMs<0)throw Error('Invalid companion timeout estimate');return Math.max(30000,estimatedTimeoutMs+5000);}
// A lost login reply is common on a busy channel; a repeated login is harmless, so retry promptly.
export const LOGIN_ATTEMPTS=3;
export function loginWaitMs(estimatedTimeoutMs){if(!Number.isFinite(estimatedTimeoutMs)||estimatedTimeoutMs<0)throw Error('Invalid companion timeout estimate');return Math.max(5000,estimatedTimeoutMs*2);}
export class RemoteReplyTimeout extends Error {constructor(detail){super(detail);this.name='RemoteReplyTimeout';}}
export function packetCounters(frame){if(frame[0]!==24||frame[1]!==2||frame.length<26)throw Error('Invalid packet counter response');const v=new DataView(frame.buffer,frame.byteOffset,frame.byteLength);return {rx:v.getUint32(2,true),tx:v.getUint32(6,true),txFlood:v.getUint32(10,true),txDirect:v.getUint32(14,true),rxFlood:v.getUint32(18,true),rxDirect:v.getUint32(22,true)};}
function sent(frame){if(frame.length<10)throw Error('Truncated SENT response');const estimatedTimeoutMs=new DataView(frame.buffer,frame.byteOffset+6,4).getUint32(0,true);return {viaFlood:!!frame[1],estimatedTimeoutMs};}
function error(frame){return Error('Companion rejected request (code '+(frame[1]??'unknown')+')');}
export class RemoteProbe {
  constructor(connection,notify=()=>{}){this.c=connection;this.notify=notify;}
  wait(codes,send,ms=5000,accept=()=>true){
    const c=this.c;
    if(c.closed)return Promise.reject(Error('Companion disconnected'));
    return new Promise((resolve,reject)=>{
      let done=false;const finish=(err,value)=>{if(done)return;done=true;clearTimeout(timer);c.off('wire',onFrame);c.pending.delete(cancel);err?reject(err):resolve(value);};
      const cancel=()=>finish(Error('Companion disconnected during remote test'));
      const onFrame=f=>{try{if(f[0]===1)finish(error(f));else if(codes.includes(f[0])&&accept(f))finish(null,f);}catch(e){finish(e);}};
      const timer=setTimeout(()=>finish(Error('Companion response timed out')),ms);
      c.on('wire',onFrame);c.pending.add(cancel);
      Promise.resolve().then(send).catch(e=>finish(e));
    });
  }
  async login(key,password){
    const bytes=new TextEncoder().encode(password);
    if(!bytes.length||bytes.length>15||/[\0\r\n]/.test(password))throw Error('Admin password must be 1–15 UTF-8 bytes without line breaks');
    const c=this.c;keyBytes(key);
    return new Promise((resolve,reject)=>{
      // One listener spans every attempt, so a late reply to an earlier attempt still completes the login.
      let result=null,ack=null,done=false,attempt=0,deadline=null,resend=null;
      const finish=(err,value)=>{if(done)return;done=true;clearTimeout(timer);clearTimeout(deadline);clearTimeout(resend);c.off('wire',onFrame);c.pending.delete(cancel);err?reject(err):resolve(value);};
      const cancel=()=>finish(Error('Disconnected during repeater login'));
      let timer=null;
      const send=()=>{attempt++;clearTimeout(timer);timer=setTimeout(()=>finish(Error('No gateway SENT response for login')),5000);
        this.notify('Login dispatch · attempt '+attempt+'/'+LOGIN_ATTEMPTS+' · command 26 · full 32-byte target key · '+bytes.length+' password bytes (contents hidden)');
        Promise.resolve().then(async()=>{await c.sendCommandSendLogin(keyBytes(key),password);this.notify('Login transport write completed');}).catch(e=>finish(e));};
      const onFrame=f=>{try{
        if(f[0]===1){finish(error(f));return;}
        if(f[0]===6){ack=sent(f);clearTimeout(timer);
          if(!deadline)deadline=setTimeout(()=>finish(Error('Repeater login timed out after '+attempt+' attempt'+(attempt>1?'s':''))),replyWaitMs(ack.estimatedTimeoutMs));
          clearTimeout(resend);if(attempt<LOGIN_ATTEMPTS)resend=setTimeout(()=>{if(result)return;this.notify('Login reply not heard; resending');send();},loginWaitMs(ack.estimatedTimeoutMs));
          this.notify('Login SENT received · attempt '+attempt+'/'+LOGIN_ATTEMPTS+' · '+(ack.viaFlood?'flood':'direct')+' · estimate '+ack.estimatedTimeoutMs+' ms · resend after '+loginWaitMs(ack.estimatedTimeoutMs)+' ms. SENT confirms acceptance, not radio TX.');}
        if([0x85,0x86].includes(f[0])){
          if(f.length<8){this.notify('Login response ignored: truncated '+f.length+'-byte frame');return;}
          const prefix=fullKey(f.slice(2,8));
          if(prefix!==key.slice(0,12)){this.notify('Login response ignored: sender '+prefix+' does not match '+key.slice(0,12));return;}
          this.notify('Login response matched selected repeater · '+prefix);
          if(f[0]===0x86){finish(Error('Repeater login rejected'));return;}
          if(f[1]!==1){finish(Error('Login did not grant verified admin access'));return;}
          result={isAdmin:true,targetPrefix:fullKey(f.slice(2,8)),permissions:f.length>12?f[12]:null,firmwareLevel:f.length>13?f[13]:null};
        }
        if(result&&ack)finish(null,{...result,gatewaySent:ack,attempts:attempt});
      }catch(e){finish(e);}};
      c.on('wire',onFrame);c.pending.add(cancel);send();
    });
  }
  async drain(){
    for(let i=0;i<1024;i++){
      const frame=await this.wait([7,8,10,16,17],()=>this.c.sendCommandSyncNextMessage());
      if(frame[0]===10)return;
    }
    throw Error('Queued-message drain exceeded 1024 records; remote test stopped');
  }
  async cli(key,command){
    if(/[\0\r\n]/.test(command)||new TextEncoder().encode(command).length>157)throw Error('Remote command must fit 157 UTF-8 bytes without line breaks/NUL');
    const c=this.c,tag=token();let reply=null,unmatched=0,polls=0;
    const capture=f=>{const m=matchingReply(f,key,tag);if(m)reply=m;else if([7,16].includes(f[0]))unmatched++;};
    c.on('wire',capture);
    try{
      this.notify('Dispatching remote CLI · tag '+tag+' · target '+key.slice(0,12));
      const ack=sent(await this.wait([6],async()=>{await c.sendCommandSendTxtMsg(1,0,0,keyBytes(key),tag+'|'+command);this.notify('Companion transport write completed · tag '+tag);}));
      const deadline=Date.now()+replyWaitMs(ack.estimatedTimeoutMs);
      this.notify('Companion SENT received · tag '+tag+' · '+(ack.viaFlood?'flood':'direct')+' · estimate '+ack.estimatedTimeoutMs+' ms · wait '+replyWaitMs(ack.estimatedTimeoutMs)+' ms. SENT confirms acceptance, not radio TX.');
      while(!reply){
        if(c.closed)throw Error('Companion disconnected; query outcome uncertain');
        if(Date.now()>=deadline)throw new RemoteReplyTimeout('Repeater reply timed out; query outcome uncertain; no retry · tag '+tag+' · queue polls '+polls+' · unmatched messages '+unmatched);
        // One local RPC at a time, independent of the outstanding LoRa wait.
        polls++;const frame=await this.wait([7,8,10,16,17],()=>c.sendCommandSyncNextMessage(),Math.min(5000,Math.max(1,deadline-Date.now())));
        if(frame[0]===10&&!reply)await new Promise(resolve=>setTimeout(resolve,250));
      }
      if(command==='get public.key'){
        const found=reply.text.match(/(?:^|\s)([a-f0-9]{64})(?=$|\s)/i);
        if(!found||found[1].toLowerCase()!==key)throw Error('Remote public key does not match the selected V4 identity');
      }
      return {command,tag,gatewaySent:ack,...reply,completionBasis:'matching-sender-cli-type-and-reflected-tag'};
    }finally{c.off('wire',capture);}
  }
}
