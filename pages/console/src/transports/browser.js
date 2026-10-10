import {detectUsb} from './usb-detection.js';
import {deferredObserver} from '../core/observer.js';
import Connection from '../vendor/meshcore/connection/connection.js';
import Constants from '../vendor/meshcore/constants.js';
import {UsbFrames,usbFrame,deviceInfo} from '../protocol.js';
// Transport bodies extracted from the hardware-verified diagnostic at be835070.
export function transportFactory(events={}){
let current=null,epoch=1,report={};
const observeActivity=deferredObserver(events.activity);
const log=events.log||(()=>{}),stage=log,status=text=>events.disconnected?.(text),controls=()=>{};
function timeout(p,ms,label){let timer;return Promise.race([p,new Promise((_,rej)=>timer=setTimeout(()=>rej(Error(label+' timed out')),ms))]).finally(()=>clearTimeout(timer));}
// Own transport lifecycle; upstream source supplies only command encoders/response parsers.
class DiagnosticConnection extends Connection {
  constructor(write){super();this.write=write;this.pending=new Set();this.closed=false;}
  async sendToRadioFrame(bytes){if(this.closed)throw Error('Connection closed');await this.write(Uint8Array.from(bytes));observeActivity({direction:'TX',bytes:bytes.length});events.frame?.('TX',bytes);}
  frame(bytes){
    observeActivity({direction:'RX',bytes:bytes.length});events.frame?.('RX',bytes);
    if(bytes[0]===0){this.onFrameReceived(bytes);return;}
    if(bytes[0]===24&&bytes.length>=2)this.emit('wire',Uint8Array.from(bytes));
    const rawMin={1:1,6:10,7:13,8:7,9:5,10:1,11:2,14:65,15:1,16:16,17:10,23:9,129:33,131:1,133:8,134:8};
    if(bytes[0]in rawMin&&bytes.length>=rawMin[bytes[0]])this.emit('wire',Uint8Array.from(bytes));
    // Only diagnostic response codes are decoded: unsolicited messages are not exported.
    const min={1:1,2:5,3:148,4:5,5:58,12:3,13:20};
    if(!(bytes[0]in min))return;
    if(bytes.length<min[bytes[0]]){log('Ignored truncated application response');return;}
    try{if(bytes[0]===13)this.emit(13,deviceInfo(bytes));else this.onFrameReceived(bytes);}catch(e){log('Malformed application response: '+e.message);}
  }
  rpc(code,send,ms=7000,onContact=null){
    if(this.closed)return Promise.reject(Error('Connection closed'));
    return new Promise((resolve,reject)=>{
      let settled=false;const finish=(err,value)=>{if(settled)return;settled=true;clearTimeout(timer);this.off(code,ok);this.off(1,bad);if(onContact)this.off(3,guardedContact);this.pending.delete(cancel);err?reject(err):resolve(value);};
      const ok=v=>finish(null,v),bad=v=>finish(Error('Companion error '+v.errCode)),cancel=()=>finish(Error('Disconnected during query'));
      const timer=setTimeout(()=>finish(Error('Companion query timed out')),ms);
      const guardedContact=v=>{try{onContact(v);}catch(e){finish(e);}};
      if(onContact)this.on(3,guardedContact);
      this.on(code,ok);this.on(1,bad);this.pending.add(cancel);
      Promise.resolve().then(send).catch(e=>finish(e));
    });
  }
  dispose(){this.closed=true;for(const cancel of [...this.pending])cancel();this.eventListenersMap.clear();}
}
async function serialTransport(port,mode,session){
  await port.open({baudRate:115200});
  if(session!==epoch){await port.close();throw Error('Connection cancelled');}
  const reader=port.readable.getReader();let closing=false;let receiver=()=>{};let decoder=new TextDecoder();
  const transport={mode,port,connected:true,setMode(next){mode=next;transport.mode=next;decoder=new TextDecoder();frames.buffer=[];if(next==='direct'){transport.protocol?.dispose();delete transport.protocol;}},receive(fn){receiver=fn;},async write(bytes){if(!transport.connected)throw Error('Device disconnected');const writer=port.writable.getWriter();try{await writer.write(bytes);if(mode!=='usb')observeActivity({direction:'TX',bytes:bytes.length});}finally{writer.releaseLock();}},async close(){if(closing)return;closing=true;transport.connected=false;transport.protocol?.dispose();receiver(null);try{await reader.cancel();}catch{}await readTask;port.removeEventListener('disconnect',lost);try{await port.close();}catch(e){log('Port close: '+e.message);}}};
  const lost=()=>{transport.connected=false;transport.protocol?.dispose();receiver(null);if(current===transport){current=null;status('Device disconnected');controls();}};
  port.addEventListener('disconnect',lost);
  const frames=new UsbFrames(frame=>transport.protocol?.frame(frame),log);
  const readTask=(async()=>{try{while(!closing){const {value,done}=await reader.read();if(done)break;if(mode!=='usb'){observeActivity({direction:'RX',bytes:value.length});const text=decoder.decode(value,{stream:true});if(mode!=='auto')events.text?.(text);receiver(text);}if(mode!=='direct')frames.feed(value);}}catch(e){if(!closing)log('Read error: '+e.message);}finally{reader.releaseLock();if(!closing){lost();try{await port.close();}catch{}}}})();
  if(mode!=='direct')transport.protocol=new DiagnosticConnection(bytes=>transport.write(usbFrame(bytes)));
  return transport;
}
async function bleTransport(device,session){
  let transport,cancelled=false;const lost=()=>{if(session!==epoch)return;if(report)(report.events??=[]).push({at:new Date().toISOString(),event:'gattserverdisconnected',stage:report.stage});log('Bluetooth disconnected at '+report?.stage);if(transport){transport.connected=false;transport.protocol?.dispose();}if(current===transport){current=null;status('Bluetooth device disconnected');controls();}};
  device.addEventListener('gattserverdisconnected',lost);
  try{
    stage('ble-gatt-connect');const connection=device.gatt.connect();connection.then(server=>{if(cancelled)server.disconnect();}).catch(()=>{});
    const server=await timeout(connection,20000,'Bluetooth connection');
    if(session!==epoch){server.disconnect();throw Error('Connection cancelled');}
    stage('ble-service-discovery',{gattConnected:server.connected});
    const service=await timeout(server.getPrimaryService(Constants.Ble.ServiceUuid.toLowerCase()),10000,'BLE service discovery');
    stage('ble-write-characteristic');const rx=await timeout(service.getCharacteristic(Constants.Ble.CharacteristicUuidRx.toLowerCase()),10000,'BLE write characteristic');
    stage('ble-notify-characteristic');const tx=await timeout(service.getCharacteristic(Constants.Ble.CharacteristicUuidTx.toLowerCase()),10000,'BLE notify characteristic');
    report.bleProperties={write:rx.properties.write,writeWithoutResponse:rx.properties.writeWithoutResponse,notify:tx.properties.notify};
    let writeQueue=Promise.resolve();
    transport={mode:'ble',device,connected:true,async close(){transport.connected=false;transport.protocol.dispose();tx.removeEventListener('characteristicvaluechanged',notify);device.removeEventListener('gattserverdisconnected',lost);try{await timeout(tx.stopNotifications(),3000,'Stop notifications');}catch{}server.disconnect();}};
    transport.protocol=new DiagnosticConnection(bytes=>{const p=writeQueue.then(()=>{if(!transport.connected||!server.connected)throw Error('GATT disconnected before command write');return timeout(rx.properties.write?rx.writeValueWithResponse(bytes):rx.writeValueWithoutResponse(bytes),7000,'BLE write');});writeQueue=p.catch(()=>{});return p;});
    const notify=e=>{const v=e.target.value;transport.protocol.frame(new Uint8Array(v.buffer,v.byteOffset,v.byteLength));};
    tx.addEventListener('characteristicvaluechanged',notify);stage('ble-enable-notifications');await timeout(tx.startNotifications(),10000,'BLE notification subscription');
    if(session!==epoch){await transport.close();throw Error('Connection cancelled');}
    return transport;
  }catch(e){cancelled=true;if(transport)await transport.close();device.removeEventListener('gattserverdisconnected',lost);device.gatt?.disconnect();throw e;}
}
return {async serial(port,mode){const candidate=await serialTransport(port,mode,epoch);current=candidate;try{if(mode==='auto')await detectUsb(candidate);return candidate;}catch(error){await candidate.close();if(current===candidate)current=null;throw error;}},async ble(device){current=await bleTransport(device,epoch);return current;},async close(){epoch++;await current?.close();current=null;}};
}
