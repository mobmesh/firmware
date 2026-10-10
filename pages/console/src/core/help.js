import {LOCAL_COMMANDS,LOCAL_ALIASES} from './local-commands.js';
import {lex} from './grammar.js';
const aliases=new Set(['?','-h','-?','/?','/h']);
export function helpRequest(raw,catalog){
  if(raw.startsWith(':raw '))return null;const ts=lex(raw);if(!ts.length)return null;
  if(aliases.has(ts[0].text))return raw.slice(ts[0].end).trim();
  const last=ts.at(-1);if(!aliases.has(last.text))return null;
  const before=raw.slice(0,last.start).trimEnd(),entry=catalog.resolve(before);
  if(entry&&entry.elements.some(e=>['tail','secret'].includes(e.kind))){const path=entry.path.join(' ');if(before.slice(path.length).trim())return null;}
  return before;
}
export function helpText(path,catalog,mode){
 const localName=LOCAL_ALIASES[path]||path;if(Object.hasOwn(LOCAL_COMMANDS,localName))return localName+'\n'+LOCAL_COMMANDS[localName];
 let entries=path?catalog.related(path):catalog.entries;
 if(path&&!entries.length){const resolved=catalog.resolve(path);if(resolved)entries=catalog.related(resolved.path.join(' '));}
 if(!entries.length)return 'No catalog help for '+path;
 const local=path?[]:Object.entries(LOCAL_COMMANDS).map(([name,description])=>name+'\n'+description);
 return [...local,...entries.map(entry=>`${entry.helpSyntax||entry.syntax}\n${entry.brief||entry.summary}`)].join('\n\n');
}

// Shown for a bare help request: console controls rather than the whole firmware catalog.
export const HELP_OVERVIEW=[
 ['connect','Open the connection dialog (also F2 or the top-right button)'],
 ['exit','Disconnect (also quit, disconnect, close)'],
 ['reconnect','Reopen the last device and resume the session'],
 ['clear','Clear the output (also cls)'],
 ['background on|off','Animated mesh background'],
 ['theme <name>','Colour theme: default, amber, grey, green, retro'],
 ['<command> ?','Help for one command; -h also works'],
 ['Tab','Complete; press twice to list matches'],
 ['↑ ↓','Command history'],
 ['F1','Command reference from the owner\'s manual'],
 ['F3','Diagnostics panel and live stream'],
 ['Ctrl+Shift+Enter','Full screen'],
 [':raw <command>','Send exactly as typed, skipping catalog checks'],
 ['Esc','Close the open dialog'],
];
export const HELP_KEY_WIDTH=Math.max(...HELP_OVERVIEW.map(([key])=>key.length))+2;
export function helpOverview(){return HELP_OVERVIEW.map(([key,text])=>key.padEnd(HELP_KEY_WIDTH)+text).join('\n');}
