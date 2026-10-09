import {lex,span,context} from './grammar.js';
export function complete(catalog,text,cursor){
  const rawOffset=text.startsWith(':raw ')?5:0;if(rawOffset){text=text.slice(5);cursor-=5;}
  const field=span(text,cursor),previous=lex(text.slice(0,field.start));const candidates=new Set(),hints=new Set();
  for(const entry of catalog.entries){const e=context(entry.elements,previous);if(!e)continue;if(e.kind==='literal'){if(e.value.startsWith(field.prefix))candidates.add(e.value);}else if(e.kind!=='separator'){for(const choice of e.choices||[])if(choice.startsWith(field.prefix))candidates.add(choice);hints.add(e.help||e.name);}}
  const values=[...candidates].sort();let replacement=null;
  if(values.length===1){replacement=values[0];if(field.end===text.length){const next=lex(text.slice(0,field.start)+replacement);if(catalog.entries.some(entry=>context(entry.elements,next)))replacement+=' ';}}
  else if(values.length>1){let common=values[0];for(const v of values)while(common&&!v.startsWith(common))common=common.slice(0,-1);if(common.length>field.prefix.length)replacement=common;}
  return {values,hint:[...hints].join('\n'),replacement:replacement===null?null:{start:field.start+rawOffset,end:field.end+rawOffset,text:replacement,cursor:field.start+rawOffset+replacement.length}};
}
