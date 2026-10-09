export class History {
  constructor(entries=[]){this.entries=entries.slice(-500);this.reset();}
  reset(){this.index=null;this.draft=null;}
  add(line){if(line&&this.entries.at(-1)!==line)this.entries.push(line);this.entries=this.entries.slice(-500);this.reset();}
  move(direction,text,cursor){if(!this.entries.length)return {text,cursor};if(this.index===null){if(direction>0)return {text,cursor};this.draft={text,cursor};this.index=this.entries.length;}this.index=Math.max(0,Math.min(this.entries.length,this.index+direction));return this.index===this.entries.length?this.draft:{text:this.entries[this.index],cursor:this.entries[this.index].length};}
}
