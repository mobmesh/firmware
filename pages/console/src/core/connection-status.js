export class ConnectionStatus {
  constructor(){this.listeners=new Set();this.snapshot=Object.freeze({revision:0,epoch:0,linkState:'disconnected',deviceConnected:false,targetKey:null,targetState:'none',canSubmit:false,persistence:'loading',lastTargetResponseAt:null});}
  getSnapshot(){return this.snapshot;}
  update(fields){this.snapshot=Object.freeze({...this.snapshot,...fields,revision:this.snapshot.revision+1});for(const fn of this.listeners)try{fn(this.snapshot);}catch{}return this.snapshot;}
  subscribe(fn){this.listeners.add(fn);fn(this.snapshot);return ()=>this.listeners.delete(fn);}
}
