// UI observations never execute inside a transport frame handler or write completion.
export function deferredObserver(callback){
  return event=>{queueMicrotask(()=>{try{callback?.(event);}catch{/* An observer failure must not affect device I/O. */}});};
}
