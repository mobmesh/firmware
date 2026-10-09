import {usbFrame} from '../protocol.js';

// Both probes are read-only. A blank line separates binary bytes from text CLI input.
export function detectUsb(transport,{timeoutMs=15000,retryMs=1000}={}) {
  return new Promise((resolve,reject)=>{
    let finished=false,text='',retry;
    const finish=(error,mode)=>{
      if(finished)return;
      finished=true;clearTimeout(deadline);clearTimeout(retry);
      transport.protocol.off(13,companion);transport.receive(()=>{});
      if(error)reject(error);else{transport.setMode(mode);resolve(mode);}
    };
    const companion=info=>{transport.deviceInfo=info;finish(null,'usb');};
    transport.protocol.on(13,companion);
    transport.receive(chunk=>{
      if(chunk===null){finish(Error('USB disconnected during identification'));return;}
      text=(text+chunk).slice(-4096);
      if(/  -> [^\r\n]*v?\d+\.\d+[^\r\n]*[\r\n]/.test(text))finish(null,'direct');
    });
    const deadline=setTimeout(()=>finish(Error('USB device did not identify as a repeater or companion')),timeoutMs);
    const probe=async()=>{
      try{
        await transport.write(usbFrame(Uint8Array.of(22,3)));
        if(finished)return;
        await transport.write(new TextEncoder().encode('\rver\r'));
        if(!finished)retry=setTimeout(probe,retryMs);
      }catch(error){finish(error);}
    };
    probe();
  });
}
