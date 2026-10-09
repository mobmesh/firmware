const validKey=key=>{if(!/^[a-f0-9]{64}$/i.test(key||''))throw Error('A full repeater key is required');return key.toLowerCase();};
const request=r=>new Promise((resolve,reject)=>{r.onsuccess=()=>resolve(r.result);r.onerror=()=>reject(r.error||Error('Credential storage failed'));});
const complete=t=>new Promise((resolve,reject)=>{t.oncomplete=resolve;t.onerror=t.onabort=()=>reject(t.error||Error('Credential transaction failed'));});
// Browser-local encryption at rest. The non-exportable key belongs to this origin.
// This is not an OS keychain: scripts trusted on this origin can recall passwords.
export class BrowserCredentialStorage {
 constructor(){this.db=null;this.opening=null;}
 async open(){if(this.db)return this.db;if(!this.opening)this.opening=new Promise((resolve,reject)=>{const r=indexedDB.open('mobmesh-admin-credentials',1);r.onupgradeneeded=()=>{r.result.createObjectStore('keys');r.result.createObjectStore('passwords');};r.onsuccess=()=>{this.db=r.result;this.db.onversionchange=()=>{this.db.close();this.db=null;this.opening=null;};resolve(this.db);};r.onerror=()=>reject(r.error);r.onblocked=()=>reject(Error('Credential storage is blocked by another tab'));}).catch(e=>{this.opening=null;throw e;});return this.opening;}
 async get(store,key){const db=await this.open();return request(db.transaction(store).objectStore(store).get(key));}
 async put(store,key,value){const db=await this.open(),t=db.transaction(store,'readwrite'),done=complete(t);t.objectStore(store).put(value,key);await done;}
 async remove(store,key){const db=await this.open(),t=db.transaction(store,'readwrite'),done=complete(t);t.objectStore(store).delete(key);await done;}
 async encryptionKey(candidate){const db=await this.open(),t=db.transaction('keys','readwrite'),done=complete(t),store=t.objectStore('keys');const existing=await request(store.get('aes'));if(!existing)store.put(candidate,'aes');await done;return existing||candidate;}
}
export class CredentialVault {
 constructor(storage=new BrowserCredentialStorage()){this.storage=storage;this.error=null;}
 async key(){let key=await this.storage.get('keys','aes');if(!key){const candidate=await crypto.subtle.generateKey({name:'AES-GCM',length:256},false,['encrypt','decrypt']);key=await this.storage.encryptionKey(candidate);}return key;}
 async read(target){const key=validKey(target);try{const record=await this.storage.get('passwords',key);if(!record)return null;const plaintext=await crypto.subtle.decrypt({name:'AES-GCM',iv:record.iv,additionalData:new TextEncoder().encode(key)},await this.key(),record.ciphertext);this.error=null;return new TextDecoder().decode(plaintext);}catch(e){this.error=e.message;return null;}}
 async save(target,password){const key=validKey(target),bytes=new TextEncoder().encode(password);if(!bytes.length||bytes.length>15||/[\0\r\n]/.test(password))throw Error('Invalid admin password');try{const iv=crypto.getRandomValues(new Uint8Array(12));const ciphertext=await crypto.subtle.encrypt({name:'AES-GCM',iv,additionalData:new TextEncoder().encode(key)},await this.key(),bytes);await this.storage.put('passwords',key,{version:1,iv,ciphertext});this.error=null;return true;}catch(e){this.error=e.message;return false;}}
 async forget(target){try{await this.storage.remove('passwords',validKey(target));this.error=null;return true;}catch(e){this.error=e.message;return false;}}
}
