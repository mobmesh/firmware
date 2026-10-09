import {fullKey} from '../protocol.js';
// Whitelist metadata only. Never retain whole frames or password bytes.
export function loginFrameMetadata(bytes,direction,wireListeners=0){
  if(direction==='TX'&&bytes[0]===26)return {direction,code:26,length:bytes.length,targetPrefix:bytes.length>=33?fullKey(bytes.slice(1,7)):null,passwordBytes:Math.max(0,bytes.length-33),wireListeners};
  if(direction==='RX'&&[133,134].includes(bytes[0]))return {direction,code:bytes[0],length:bytes.length,senderPrefix:bytes.length>=8?fullKey(bytes.slice(2,8)):null,isAdmin:bytes[0]===133&&bytes.length>=2?bytes[1]:null,wireListeners};
  return null;
}
