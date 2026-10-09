import {wireInput} from '../core/grammar.js';
export function directCommand(transport,raw,{kind='marked',signal}={}){
  const text=wireInput(raw,'direct');
  return new Promise((resolve,reject)=>{
    let content='',idle,done=false,replyStarted=false;const stream=kind==='stream'||kind==='eof'||kind==='interactive';
    const finish=(err,value)=>{if(done)return;done=true;clearTimeout(timer);clearTimeout(idle);signal?.removeEventListener('abort',abort);transport.receive(()=>{});err?reject(err):resolve(value);};
    const abort=()=>finish(Error('Wait cancelled; execution outcome uncertain'));
    const result=basis=>({text:content,completionBasis:basis});
    const expired=()=>finish(Error('Reply deadline reached; execution outcome uncertain'));
    // Slot status verifies the inactive image from flash before producing its reply.
    const waitMs=kind==='no-reply'?1500:kind==='eof'?60000:stream?15000:text.trim()==='get ota.slot'?30000:5000;
    let timer=setTimeout(()=>kind==='no-reply'?finish(null,result('sent-no-result')):expired(),waitMs);
    if(signal?.aborted){abort();return;}signal?.addEventListener('abort',abort,{once:true});
    transport.receive(chunk=>{if(chunk===null){kind==='no-reply'?finish(null,result('disconnected-after-send')):finish(Error('Disconnected while waiting; execution outcome uncertain'));return;}content+=chunk;if(content.length>2*1024*1024){finish(Error('Response exceeded retention limit'));return;}if(kind==='eof'&&content.includes('   EOF')){finish(null,result('eof'));return;}if(kind!=='eof'&&(content.includes('  -> ')||(stream&&content.includes(text)))){
      // Receipt ends the first-reply deadline; assembly has its own bounded window.
      if(!replyStarted){replyStarted=true;clearTimeout(timer);timer=setTimeout(expired,stream?15000:5000);}
      clearTimeout(idle);
      const marker=content.indexOf('  -> ');
      if(stream||(marker>=0&&content.slice(marker+5).trim()&&content.endsWith('\n'))){
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
  if(lines[0]?.startsWith('  -> '))lines[0]=lines[0].slice(5);
  return lines.join('\n').replace(/\n+$/,'');
}
