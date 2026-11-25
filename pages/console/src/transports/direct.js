import {wireInput} from '../core/grammar.js';
const SLOW_REPLIES={'get ota.slot':30000,'ota wan join':30000,'ota wan check':75000};
// Output that arrives between commands, such as a late reply, prints above the prompt instead of being dropped.
let idlePrint=null;
// A reply starts with "  -> ", or with "  ->" on a line of its own when it spans several lines.
const REPLY_MARK=/  ->[ \r\n]/;
export function setDirectIdle(print){idlePrint=print;}
function idleReceiver(){
  let buffer='',quiet=null;
  const flush=()=>{quiet=null;const text=buffer.replace(/\r\n?/g,'\n').split('\n').map(line=>line==='  ->'?'':line.startsWith('  -> ')?line.slice(5):line).join('\n').replace(/^\n+|\n+$/g,'');buffer='';if(text.trim())idlePrint?.(text);};
  return chunk=>{if(chunk===null||!idlePrint)return;buffer+=chunk;clearTimeout(quiet);quiet=setTimeout(flush,250);};
}
export function directCommand(transport,raw,{kind='marked',signal}={}){
  const text=wireInput(raw,'direct');
  return new Promise((resolve,reject)=>{
    let content='',idle,done=false,replyStarted=false;const stream=kind==='stream'||kind==='eof'||kind==='interactive';
    const finish=(err,value)=>{if(done)return;done=true;clearTimeout(timer);clearTimeout(idle);signal?.removeEventListener('abort',abort);transport.receive(idleReceiver());err?reject(err):resolve(value);};
    const abort=()=>finish(Error('Wait cancelled; execution outcome uncertain'));
    const result=basis=>({text:content,completionBasis:basis});
    const expired=()=>finish(Error('Reply deadline reached; execution outcome uncertain'));
    // Some commands work before replying: slot status hashes the inactive image, a WiFi join waits
    // up to 15 s plus a 5 s clock sync, and a WAN check makes up to three 20 s requests.
    const waitMs=kind==='no-reply'?1500:kind==='eof'?60000:stream?15000:SLOW_REPLIES[text.trim()]||5000;
    let timer=setTimeout(()=>kind==='no-reply'?finish(null,result('sent-no-result')):expired(),waitMs);
    if(signal?.aborted){abort();return;}signal?.addEventListener('abort',abort,{once:true});
    transport.receive(chunk=>{if(chunk===null){kind==='no-reply'?finish(null,result('disconnected-after-send')):finish(Error('Disconnected while waiting; execution outcome uncertain'));return;}content+=chunk;if(content.length>2*1024*1024){finish(Error('Response exceeded retention limit'));return;}if(kind==='eof'&&content.includes('   EOF')){finish(null,result('eof'));return;}if(kind!=='eof'&&(REPLY_MARK.test(content)||(stream&&content.includes(text)))){
      // Receipt ends the first-reply deadline; assembly has its own bounded window.
      if(!replyStarted){replyStarted=true;clearTimeout(timer);timer=setTimeout(expired,stream?15000:5000);}
      clearTimeout(idle);
      const marker=content.search(REPLY_MARK);
      if(stream||(marker>=0&&content.slice(marker+4).trim()&&content.endsWith('\n'))){
        idle=setTimeout(()=>finish(null,result(stream?'stream-idle':'marker-idle')),stream?500:250);
      }
    }});
    transport.write(new TextEncoder().encode(text+'\r')).catch(e=>finish(e));
  });
}

export async function directVersion(transport){
  // USB can enumerate before the firmware's command loop is ready.
  for(let attempt=0;attempt<12;attempt++){
    try{
      const reply=await directCommand(transport,'ver');
      if(/  -> [^\r\n]*v?\d+\.\d+/.test(reply.text))return reply;
    }catch(error){
      if(!error.message.startsWith('Reply deadline reached'))throw error;
    }
    if(attempt<11)await new Promise(resolve=>setTimeout(resolve,250));
  }
  throw Error('Device did not return a firmware version');
}

// The serial CLI echoes the typed line and prefixes its reply with "  -> "; show only the reply.
export function displayReply(command,text){
  let lines=text.replace(/\r\n?/g,'\n').split('\n');
  if(lines[0].trim()===command.trim())lines=lines.slice(1);
  if(lines[0]?.replace(/\s+$/,'')==='  ->')lines=lines.slice(1);
  else if(lines[0]?.startsWith('  -> '))lines[0]=lines[0].slice(5);
  return lines.join('\n').replace(/\n+$/,'');
}
