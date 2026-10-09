import {fullKey} from '../protocol.js';
import {keyBytes} from './remote.js';
export class TemporaryTargets {
  constructor(){this.created=new Map();}
  validate(value,contacts,gatewayKey){
    const key=value.trim().toLowerCase();keyBytes(key);
    if(key===gatewayKey)throw Error('Enter a repeater key, not the connected companion key');
    const matches=contacts.filter(c=>fullKey(c.publicKey).startsWith(key.slice(0,12)));
    if(matches.some(c=>fullKey(c.publicKey)!==key))throw Error('Another contact shares this six-byte prefix; remote addressing is ambiguous');
    const existing=matches.find(c=>fullKey(c.publicKey)===key);
    if(existing&&existing.type!==2)throw Error('This key is already a non-repeater contact');
    return {key,existing};
  }
  async ensure(value,contacts,gatewayKey,connection,pathLength=255){
    if(pathLength!==255)throw Error('This skeleton uses flood discovery');
    const {key,existing}=this.validate(value,contacts,gatewayKey);
    const name='Console '+key.slice(0,16);
    if(existing){if(existing.advName!==name)return {key,temporary:this.created.has(key)};this.created.set(key,name);if((existing.outPathLen&255)!==pathLength)await connection.rpc(0,()=>connection.sendCommandAddUpdateContact(keyBytes(key),2,existing.flags||0,pathLength,new Uint8Array(64),name,existing.lastAdvert||0,existing.advLat||0,existing.advLon||0));return {key,temporary:true};}
    // Track before writing: failed acknowledgement may still mean the add occurred.
    this.created.set(key,name);
    await connection.rpc(0,()=>connection.sendCommandAddUpdateContact(keyBytes(key),2,0,pathLength,new Uint8Array(64),name,0,0,0));
    return {key,temporary:true};
  }
  async cleanup(connection,contacts,report){
    for(const [key,name] of [...this.created]){
      const found=contacts.find(c=>fullKey(c.publicKey)===key);
      if(!found){this.created.delete(key);continue;}
      if(found.advName!==name||found.type!==2){report('Temporary target retained because its contact metadata changed: '+key.slice(0,12));this.created.delete(key);continue;}
      try{await connection.rpc(0,()=>connection.sendCommandRemoveContact(keyBytes(key)));this.created.delete(key);report('Temporary target removed from companion: '+key.slice(0,12));}
      catch(e){report('Temporary target cleanup failed; companion entry may remain: '+key.slice(0,12)+' · '+e.message);}
    }
  }
}
