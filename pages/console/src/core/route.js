// Describes a contact's stored route; the path byte packs hop count (low 6 bits) and hash size minus one (top 2 bits).
const UNKNOWN=255;
const hex=bytes=>Array.from(bytes,b=>b.toString(16).padStart(2,'0')).join('');
export function routeHops(rawLen,path){
  const len=rawLen&255;if(len===UNKNOWN)return null;
  const count=len&63,size=(len>>6)+1;
  return Array.from({length:count},(_,i)=>hex(path.slice(i*size,i*size+size)));
}
// Short hashes can match several repeaters; every match is shown rather than a guess.
export function hopName(prefix,contacts){
  const names=contacts.filter(c=>hex(c.publicKey).startsWith(prefix)).map(c=>c.advName);
  return names.length?names.join('|'):prefix;
}
export function routeText(rawLen,path,contacts){
  const hops=routeHops(rawLen,path);
  if(hops===null)return 'Route: none stored · flood';
  if(!hops.length)return 'Route: direct';
  return `Route: ${hops.length} hop${hops.length>1?'s':''} · `+hops.map(h=>hopName(h,contacts)).join(' › ');
}
