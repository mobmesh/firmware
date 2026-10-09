import {RemoteProbe} from './remote.js';
import {fullKey} from '../protocol.js';
export async function initializeCompanion(connection,knownDeviceInfo=null){
 const deviceInfo=knownDeviceInfo||await connection.rpc(13,()=>connection.sendCommandDeviceQuery(3));
 const selfInfo=await connection.rpc(5,()=>connection.sendCommandAppStart());
 const contacts=[];
 await connection.rpc(4,()=>connection.sendCommandGetContacts(),15000,c=>{if(contacts.length>=2048)throw Error('Contact limit exceeded');contacts.push(c);});
 return {deviceInfo,selfInfo,contacts,gatewayKey:fullKey(selfInfo.publicKey)};
}
export async function authenticateRepeater(connection,key,password,notify=()=>{}){
 const probe=new RemoteProbe(connection,notify);
 const login=await probe.login(key,password);password='';
 notify('Repeater admin access verified');
 await probe.drain();
 const queries=[];
 queries.push(await probe.cli(key,'get public.key'));
 return {login,queries};
}
