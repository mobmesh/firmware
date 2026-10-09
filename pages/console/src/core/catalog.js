export class Catalog {
  constructor(entries,manifest){this.entries=entries;this.manifest=manifest;}
  resolve(line){const raw=line.startsWith(':raw ')?line.slice(5):line;return this.entries.filter(e=>raw===e.path.join(' ')||raw.startsWith(e.path.join(' ')+' ')).sort((a,b)=>b.path.join(' ').length-a.path.join(' ').length||(a.origin==='mobmesh'?-1:1))[0]||null;}
  related(path){return this.entries.filter(e=>e.path.join(' ')===path||e.path.join(' ').startsWith(path+' '));}
  support(entry,mode){if(!entry)return 'unknown';return mode!=='direct'&&entry.remote===false?'unsupported':'unknown';}
}
export async function loadCatalog(){const root=new URL('../catalog/',import.meta.url);const [up,mods,manifest]=await Promise.all(['upstream.json','mobmesh.json','manifest.json'].map(async f=>{const response=await fetch(new URL(f,root));if(!response.ok)throw Error('Catalog unavailable: '+f);return response.json();}));return new Catalog([...up,...mods],manifest);}
