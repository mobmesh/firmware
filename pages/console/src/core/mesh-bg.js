// Drifting mesh background, ported from the mobmesh.org landing page (website repo, public/mesh-bg.js).
const COUNT=56,COLORS=['#e8f0f8','#2dd1bd','#11b6a4'],LINK_DISTANCE=250;
export function startMeshBackground(canvas){
  const ctx=canvas.getContext?.('2d');if(!ctx)return;
  let width=0,height=0;
  const resize=()=>{const rect=canvas.getBoundingClientRect();width=canvas.width=rect.width;height=canvas.height=rect.height;};
  window.addEventListener('resize',resize);resize();
  const particles=Array.from({length:COUNT},()=>({x:Math.random()*width,y:Math.random()*height,vx:(Math.random()-.5)*.5,vy:(Math.random()-.5)*.5,radius:Math.random()*3+1.5,color:COLORS[Math.floor(Math.random()*COLORS.length)],alpha:(Math.random()*.5+.3)*.7}));
  const still=matchMedia?.('(prefers-reduced-motion: reduce)').matches;let running=false,handle=0;
  const frame=()=>{
    ctx.clearRect(0,0,width,height);
    for(const p of particles){
      if(!still){p.x+=p.vx;p.y+=p.vy;if(p.x<0)p.x=width;if(p.x>width)p.x=0;if(p.y<0)p.y=height;if(p.y>height)p.y=0;}
      ctx.beginPath();ctx.arc(p.x,p.y,p.radius,0,Math.PI*2);ctx.fillStyle=p.color+Math.floor(p.alpha*255).toString(16).padStart(2,'0');ctx.fill();
    }
    for(let i=0;i<particles.length;i++)for(let j=i+1;j<particles.length;j++){
      const a=particles[i],b=particles[j],distance=Math.hypot(a.x-b.x,a.y-b.y);
      if(distance<LINK_DISTANCE){ctx.beginPath();ctx.moveTo(a.x,a.y);ctx.lineTo(b.x,b.y);ctx.strokeStyle=`rgba(232, 240, 248, ${.1*(1-distance/LINK_DISTANCE)})`;ctx.lineWidth=2;ctx.stroke();}
    }
    if(!still&&running)handle=requestAnimationFrame(frame);
  };
  return {
    start(){if(running)return;running=true;canvas.hidden=false;resize();frame();},
    stop(){running=false;cancelAnimationFrame(handle);ctx.clearRect(0,0,width,height);canvas.hidden=true;},
  };
}
