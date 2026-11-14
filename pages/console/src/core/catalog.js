const MONTHS=['jan','feb','mar','apr','may','jun','jul','aug','sep','oct','nov','dec'];
// A `ver` reply: "v1.18.0 (15 Oct 2026) + MobMesh (21 Oct 2026 - 75a1ec6)", or "+ ota" from older builds; only MobMesh ESP32 builds add that part, bench builds omit its hash.
export function parseBuild(text){
  const version=/v?(\d+\.\d+\.\d+)/.exec(text||'');if(!version)return null;
  // Any "+ MobMesh (...)" is a MobMesh build: release tags carry a hex commit, local and bench builds may carry another tag or an ISO date.
  const tag=/\+ (?:ota|MobMesh) \(([^)]*)\)/.exec(text||'');if(!tag)return {upstream:version[1],ota:false,sha:null,date:null};
  const long=/(\d{1,2}) (\w{3}) (\d{4})/.exec(tag[1]),iso=/(\d{4})-(\d{2})-(\d{2})/.exec(tag[1]),month=long?MONTHS.indexOf(long[2].toLowerCase()):-1;
  const date=long&&month>=0?`${long[3]}-${String(month+1).padStart(2,'0')}-${long[1].padStart(2,'0')}`:iso?`${iso[1]}-${iso[2]}-${iso[3]}`:null;
  const sha=/ - ([0-9a-f]{7,40})$/.exec(tag[1])?.[1]||null;
  return {upstream:version[1],ota:true,sha,date};
}
const older=(a,b)=>{const x=a.split('.').map(Number),y=b.split('.').map(Number);for(let i=0;i<3;i++)if(x[i]!==y[i])return x[i]<y[i];return false;};
export class Catalog {
  constructor(entries,manifest){this.entries=entries;this.manifest=manifest;this.builds=manifest?.builds||[];this.setBuild(null);}
  // No build read yet, or one the catalog cannot place, counts as the newest build: retired commands hide, nothing else does.
  setBuild(build){
    this.build=build;this.buildIndex=this.builds.length-1;
    if(build?.ota&&this.builds.length){const listed=build.sha?this.builds.findIndex(([sha])=>build.sha.startsWith(sha)):-1;
      if(listed>=0)this.buildIndex=listed;
      else if(build.date&&build.date<this.builds.at(-1)[1]){const index=this.builds.findLastIndex(([,date])=>date<=build.date);this.buildIndex=Math.max(0,index);}}
    this.usable=this.entries.filter(e=>!this.availability(e));
  }
  at(sha){return this.builds.findIndex(([listed])=>listed===sha);}
  // Null when the connected node has the command, else why not and what replaced it.
  availability(entry){
    if(entry.origin==='upstream')return entry.sinceUpstream&&this.build&&older(this.build.upstream,entry.sinceUpstream)?{reason:'needs MeshCore '+entry.sinceUpstream+' or later'}:null;
    if(entry.mod==='hotspot-ota'&&!entry.stock&&this.build&&!this.build.ota)return {reason:'MobMesh ESP32 firmware only'};
    const until=entry.until?this.at(entry.until):-1;if(until>=0&&this.buildIndex>=until)return {reason:'removed in the build of '+this.builds[until][1],replacedBy:entry.replacedBy||null};
    const since=entry.since?this.at(entry.since):-1;if(since>this.buildIndex)return {reason:'needs a build from '+this.builds[since][1]+' or later'};
    return null;
  }
  resolve(line){const raw=line.startsWith(':raw ')?line.slice(5):line;return this.entries.filter(e=>raw===e.path.join(' ')||raw.startsWith(e.path.join(' ')+' ')).sort((a,b)=>b.path.join(' ').length-a.path.join(' ').length||!!this.availability(a)-!!this.availability(b)||(a.origin==='mobmesh'?-1:1))[0]||null;}
  related(path){return this.entries.filter(e=>e.path.join(' ')===path||e.path.join(' ').startsWith(path+' '));}
  support(entry,mode){if(!entry)return 'unknown';return mode!=='direct'&&entry.remote===false?'unsupported':'unknown';}
}
export async function loadCatalog(){const root=new URL('../catalog/',import.meta.url);const [up,mods,manifest]=await Promise.all(['upstream.json','mobmesh.json','manifest.json'].map(async f=>{const response=await fetch(new URL(f,root));if(!response.ok)throw Error('Catalog unavailable: '+f);return response.json();}));return new Catalog([...up,...mods],manifest);}
