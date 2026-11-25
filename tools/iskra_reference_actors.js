// Drawing routines from the supplied Iskra artwork.
function makeCanvas(w,h){const c=document.createElement('canvas');c.width=w;c.height=h;return c;}
function sprite(w,h,fn){const c=makeCanvas(w,h);fn(c.getContext('2d'),w,h);return c;}
function round(x,y,w,h,r,fill,stroke){ctx.beginPath();ctx.roundRect(x,y,w,h,r);if(fill){ctx.fillStyle=fill;ctx.fill();}if(stroke){ctx.strokeStyle=stroke;ctx.stroke();}}
function ellipse(x,y,rx,ry,fill,rot=0){ctx.beginPath();ctx.ellipse(x,y,rx,ry,rot,0,TAU);ctx.fillStyle=fill;ctx.fill();}
function line(x1,y1,x2,y2,color,width=1){ctx.beginPath();ctx.moveTo(x1,y1);ctx.lineTo(x2,y2);ctx.strokeStyle=color;ctx.lineWidth=width;ctx.lineCap='round';ctx.stroke();}
// Reuse soft halos. No new screen-sized blur surfaces per spark per frame.
const haloSprites=new Map();
function glow(x,y,r,color,alpha=1){
 if(r<=0||alpha<=0)return;
 let halo=haloSprites.get(color);
 if(!halo){
  halo=sprite(128,128,(g)=>{const gr=g.createRadialGradient(64,64,0,64,64,64);
   gr.addColorStop(0,color);gr.addColorStop(1,'transparent');
   g.fillStyle=gr;g.fillRect(0,0,128,128);});
  haloSprites.set(color,halo);
 }
 ctx.save();ctx.globalAlpha*=clamp(alpha);ctx.drawImage(halo,x-r,y-r,r*2,r*2);ctx.restore();
}
function poly(points,fill){ctx.beginPath();points.forEach((p,i)=>i?ctx.lineTo(...p):ctx.moveTo(...p));ctx.closePath();ctx.fillStyle=fill;ctx.fill();}
function bezier(points,color,width=1){ctx.beginPath();ctx.moveTo(...points[0]);for(let i=1;i<points.length;i+=3)ctx.bezierCurveTo(...points[i],...points[i+1],...points[i+2]);ctx.strokeStyle=color;ctx.lineWidth=width;ctx.lineCap='round';ctx.stroke();}
function gradient(x,y,x2,y2,stops){const g=ctx.createLinearGradient(x,y,x2,y2);stops.forEach(s=>g.addColorStop(...s));return g;}
function limb(ax,ay,bx,by,cx,cy,color){line(ax,ay,bx,by,'#102c35',20);line(ax,ay,bx,by,color,13);ellipse(bx,by,11,11,'#17363e');ellipse(bx-2,by-2,6,6,'#8ab1a0');line(bx,by,cx,cy,'#112d34',16);line(bx,by,cx,cy,color,10);ellipse(cx,cy,9,9,'#99bfa8');}
function lantern(x,y,angle,lit){ctx.save();ctx.translate(x,y);ctx.rotate(angle);
 if(capturingFlame){const pos=ctx.getTransform().transformPoint({x:0,y:46});
   flameAnchors.push([pos.x,pos.y]);}
 ellipse(0,13,16,21,'#15333c');ellipse(0,13,12,16,'#3b5b58');round(-21,18,42,53,7,'#263e43');round(-16,23,32,40,4,gradient(-16,23,16,60,[[0,lit?'#ffae46':'#698f8b'],[.5,lit?'#fff0aa':'#3b646b'],[1,lit?'#dfa34e':'#294951']]));
 if(lit){glow(0,44,90,'#ffbb4580',lit);const fl=1+Math.sin(clock*11)*.12+Math.sin(clock*23)*.06;ellipse(-1,46,9*fl,15*fl,'#fff4bb');ellipse(0,49,3,6,'#fff8dc');}else{line(-7,43,7,51,'#adc0a977',1);}
 round(-24,14,48,9,3,'#b98244');line(-18,15,14,15,'#fff0bd',3);round(-23,66,46,9,3,'#8d5941');line(-19,67,18,67,'#ffd687',3);line(-16,25,-16,63,'#d6c99b99',2);line(16,25,16,63,'#9b885f',3);line(-8,27,-8,58,'#d9e7be55',2);ctx.restore();}
function robot(s){
 const gait=Math.cos(s.phase), running=s.kind==='Run', travelling=running||s.kind==='Walk';
 const bounce=travelling?Math.sin(s.phase*2)*(running?5:3.4):0;
 const idle=travelling?0:Math.sin(s.phase)*2.8;
 const sit=1-s.stand,rx=s.x-s.cam,ry=685+Math.sin(s.x*.004)*4;
 const lean=running?.095:travelling?.045:0;
 ctx.save();ctx.translate(rx,ry);
 if(s.light)glow(-20,-90,125,'#ffd27116',s.light);
 for(const side of [-1,1]){
   const phase=(s.phase/TAU+(side<0?.5:0))%1;
   const plant=phase<.5, progress=(phase*2)%1;
   const stride=running?47:37;
   let hx=side*20,hy=-48+sit*24+bounce*.4,kx=hx+8,ky=-26,fx=hx+4,fy=-3,angle=0;
   if(travelling){
     // The foot moves linearly across the ground during stance, then follows
     // an elevated return arc. Knee and toe reach maximum lift successively.
     const lift=plant?0:Math.sin(progress*Math.PI);
     const travel=plant?1-2*progress:-Math.cos(progress*Math.PI);
     fx=hx+stride*travel;fy-=lift*(running?33:24);
     kx=hx+(fx-hx)*.52+lift*(running?29:21);ky=(hy+fy)/2-lift*9;
     angle=plant?(.45*progress-.14):(.25-.8*progress);
   } else if(s.kind==='Jump'||s.kind==='Fall'){
     const rise=s.kind==='Jump', fold=rise?s.progress:1-s.progress;
     fx=hx+(side>0?19+23*fold:-30+12*fold);
     fy-=side>0?9+34*fold:3+14*fold;
     kx=hx+(side>0?20+19*fold:6+8*fold);ky=-34-18*fold;
     angle=side*(rise?-.21:.28);
   } else if(s.kind==='Brake'){
     fx=hx+(side>0?35:-16);fy-=side<0?4:0;kx=hx+(side>0?22:-2);angle=side>0?-.15:.24;
   } else if(s.kind==='Swim'||s.kind==='Climb'){
     fx=hx+side*gait*(s.kind==='Swim'?26:17);fy-=Math.max(0,side*gait)*26;
     kx=hx+(s.kind==='Swim'?12:20);ky-=Math.max(0,side*gait)*13;angle=side*gait*.25;
   } else{fx+=sit*side*17;kx+=sit*side*16;}
   limb(hx,hy,kx,ky,fx,fy,side<0?'#385661':'#87aaae');
   ctx.save();ctx.translate(fx,fy);ctx.rotate(angle);
   round(-15,-7,36,14,5,gradient(-10,-8,10,6,[[0,'#d2d9c4'],[.42,'#779799'],[1,'#3c5863']]));
   line(-12,6,18,6,'#223341',3);ctx.restore();
 }
 ctx.translate(0,sit*24+bounce-idle);ctx.rotate(s.walk*gait*.019+lean);
 const wave=s.happy*Math.sin(s.t*3)*8;
 const arc=s.kind==='Jump'?s.progress:s.kind==='Fall'?1-s.progress:0;
 const counter=travelling?gait*(running?31:23):s.kind==='Swim'?gait*17:-20*arc;
 limb(-49,-125,-60+counter*.6,-93,-65+counter,-58-wave-Math.max(0,counter)*.48,'#527f78');
 ctx.lineWidth=3;round(-51,-147,102,105,22,gradient(-51,-145,51,-40,[[0,'#d3dbe0'],[.22,'#95a9be'],[.55,'#586c91'],[1,'#35455f']]),'#173a42');
 round(-40,-137,80,87,17,gradient(-40,-136,40,-56,[[0,'#c0cbd6'],[.43,'#92a3c0'],[1,'#4a567e']]));
 poly([[18,-136],[35,-130],[38,-69],[23,-54],[16,-58]],'#e2b37a54');
 line(-35,-134,-12,-134,'#f0eed8',3);;
 ctx.save();ctx.globalAlpha=.24;line(-37,-128,-37,-75,'#ddedcb',3);line(-27,-61,26,-61,'#c5d7b8',1);ctx.restore();
 glow(0,-103,34,'#ffe9a012',.5+s.happy*.5);
 ellipse(0,-103,24,24,'#254d52');ellipse(0,-103,19,19,'#9dbca6');ellipse(0,-103,15,15,'#1e424b');
 const power=lerp(.48,1,s.happy);ctx.beginPath();ctx.arc(0,-103,12,-Math.PI*.8,-Math.PI*.8+Math.PI*1.6*power);ctx.lineWidth=3;ctx.strokeStyle=s.happy?'#e5d58b':'#75a69a';ctx.stroke();line(0,-103,Math.cos(lerp(-2.5,.9,power))*9,-103+Math.sin(lerp(-2.5,.9,power))*9,'#e4dcaa',2);ellipse(0,-103,2,2,'#eddfa2');
 for(const x of [-32,32])for(const y of [-127,-62]){ellipse(x,y,3,3,'#365f61');line(x-1,y,x+1,y,'#bfd3b2',1);}
 round(-19,-71,38,7,2,'#254d52');for(let j=0;j<4;j++)round(-16+j*9,-69,5,2,1,'#678d7f');
 // scarf
 const wind=(s.wind||0),fl=Math.sin(s.t*7)* (4+wind*7);
 round(-31,-163,62,18,6,'#d193e9');line(-24,-153,24,-153,'#775190',1.5);
 ctx.beginPath();ctx.moveTo(-20,-152);ctx.bezierCurveTo(-42-wind*10,-146+fl*.4,-56-wind*16,-134+fl,-66-wind*20,-120+fl*1.3);ctx.lineTo(-57-wind*20,-106+fl*1.3);ctx.bezierCurveTo(-48-wind*14,-128+fl,-36,-140,-18,-146);ctx.closePath();ctx.fillStyle='#b278d3';ctx.fill();bezier([[-23,-150],[-42-wind*9,-143],[-54-wind*14,-134+fl],[-63-wind*19,-119+fl]],'#e6c3f3',2.8);
 round(-17,-154,35,12,4,'#8e59a7');for(let j=0;j<3;j++)line(-15,-154+j*4,15,-154+j*4,'#d99fdf',1.25);
 ctx.save();ctx.translate(0,-184);const tilt=lerp(-.09,.075,s.look)-s.give*.15+s.walk*gait*.024;ctx.rotate(tilt);
 line(-7,-41,-9,-71,'#7fa18d',3);bezier([[-9,-71],[-13,-76],[1,-76],[-4,-85]],'#799d8c',2);ellipse(-4,-88,5,5,s.happy?'#e9b95e':'#efbd7d');glow(-4,-88,22,'#ffda7c55',.35+s.happy*.65);
 round(-70,-21,13,34,5,'#5c8177');ellipse(-64,-4,6,11,'#aac2a3');round(57,-21,13,34,5,'#3b6565');ellipse(64,-4,5,10,'#739e8b');
 ctx.lineWidth=3;round(-59,-43,118,83,19,gradient(-59,-43,59,40,[[0,'#faf5db'],[.25,'#bdcddb'],[.7,'#7792ad'],[1,'#444668']]),'#19273c');
 bezier([[-52,-27],[-49,-41],[-29,-40],[-14,-40]],'#fff6d7',4);
 bezier([[56,1],[57,20],[55,29],[43,34]],'#e5b881',3);;
 round(-49,-32,98,61,13,'#142f3b');round(-45,-29,90,54,10,gradient(0,-29,0,25,[[0,'#253e4e'],[1,'#102633']]));
 poly([[-42,-26],[-13,-26],[17,24],[-10,24]],'#b5dec30b');line(-45,-34,40,-34,'#d6e2b988',1.5);
 // moon rim light on the shell
 ctx.save();ctx.globalAlpha=.35;ctx.beginPath();ctx.arc(0,-2,56,-2.5,-1.2);ctx.strokeStyle='#dff2d8';ctx.lineWidth=2;ctx.stroke();ctx.restore();
 const blink=(Math.sin(s.t*1.33+.7)>.988&&s.t>2)?.13:1;const gaze=s.look*5-s.give*1;
 for(const side of [-1,1]){
   const ex=side*21+gaze,ey=-5-s.give*4;
   if(s.happy>.7){bezier([[ex-9,ey+3],[ex-7,ey-8],[ex+7,ey-8],[ex+9,ey+3]],'#b3ffe4',4);}
   else{glow(ex,ey,17,'#77ffdf88');round(ex-8,ey-11*blink,16,22*blink,Math.min(5,11*blink),'#a7fff2');ellipse(ex-2,ey-4,2,2*blink,'#edfff6');}
 }
 if(s.happy){ctx.globalAlpha=s.happy*.4;ellipse(-34,9,6,3,'#d3a967');ellipse(34,9,6,3,'#d3a967');ctx.globalAlpha=1;}
 bezier([[-8,15],[-2,18+s.happy*4],[5,18+s.happy*4],[10,14]],'#86b2a1',2);
 for(const x of [-51,51]){ellipse(x,-29,2,2,'#dae0b4');ellipse(x,26,2,2,'#aac69e');}ctx.restore();
 const reach=s.give;
 const handX=80+reach*34-counter*.57;
 const handY=-83-reach*72-Math.max(0,-counter)*.52-arc*25+(travelling?Math.sin(s.phase)*3:0);
 limb(50,-122,66+reach*13-counter*.5,-101-reach*36,handX,handY,'#81af9b');
 round(handX-10,handY-6,19,15,5,'#a8c4a5');line(handX+1,handY+1,handX+7,handY+1,'#4e7870',1);
 lantern(handX,handY+3,Math.sin(s.phase-.88)*(travelling?(running?.30:.19):.043),.85);
 ctx.restore();
}
function spark(x,y,r,alpha=1,i=0,light=true){
 const pulse=.84+.16*Math.sin(clock*3+i);ctx.save();ctx.globalAlpha=alpha;
 glow(x,y,r*4.6,'#ffc15d65',pulse);
 ctx.save();ctx.translate(x,y);ctx.rotate(Math.sin(clock*1.8+i)*.13);
 poly([[0,-r*1.36],[r*.34,-r*.32],[r,0],[r*.31,r*.26],[0,r*1.32],[-r*.3,r*.3],[-r,0],[-r*.33,-r*.29]],'#f8cb79');
 poly([[0,-r*1.36],[r*.34,-r*.32],[0,0],[-r,0]],'#fff4c5');
 poly([[0,0],[r,0],[r*.31,r*.26],[0,r*1.32]],'#ec9f53');
 ellipse(0,0,r*.21,r*.23,'#fff8df');ctx.restore();ctx.restore();if(light)addLight(x,y,41,.47);
}
function drawEnemy(e){if(e.dead)return;const x=e.x-cam,y=e.y;if(x<-80||x>W+80)return;ctx.save();ctx.translate(x+e.w/2,y+e.h/2);
 if(e.type==='beetle'){
  for(const far of [true,false])for(let j=0;j<3;j++){
    const a=gaitAngle+(j+(far?1:0))*Math.PI, lift=Math.max(0,-Math.sin(a));
    const hx=-14+j*13,kx=hx+Math.cos(a-.6)*3+(j-1)*2,tx=hx+Math.cos(a)*6+(j-1)*3;
    const ky=13-lift*4,ty=(far?16:19)-lift*6, tone=far?'#55566b':'#c0afbc';
    line(hx,6,kx,ky,tone,2.25);line(kx,ky,tx,ty,tone,2.15);line(tx,ty,tx+2,ty,tone,1.8);
  }
  ctx.translate(0,-Math.abs(Math.sin(gaitAngle))*1.15);ellipse(0,1,24,16,'#202437');ellipse(0,-1,22,15,gradient(-20,-14,20,12,[[0,'#d3b5db'],[.43,'#9b77ae'],[.72,'#775988'],[1,'#453954']]));
  bezier([[-20,-6],[-18,-12],[-8,-15],[-2,-14]],'#e4cad9',1.5);
  bezier([[0,-13],[-1,-7],[-2,-4],[-1,-2]],'#6f3e69',1.5);
  for(const d of [-1,1]){
    bezier([[d*2,-4],[d*7,-3],[d*12,-8],[d*17,-6]],'#bea0c7',1.6);
    ellipse(d*15,-1,2.9,2.2,'#6d4877');
    ellipse(d*8,-9,1.6,1.9,'#f1c29c');
  }
  for(const side of [-1,1]){ellipse(side*8,4,4,5,'#233544');ellipse(side*8,5,2,3,'#f6ce88');glow(side*8,5,7,'#f6ce8844');}
  const aw=Math.sin(gaitAngle)*2;line(-12,-12,-16,-22+aw,'#b0b2a0',1.5);line(12,-12,16,-22-aw,'#b0b2a0',1.5);ellipse(-16,-22+aw,2,2,'#dbc79a');ellipse(16,-22-aw,2,2,'#dbc79a');}
 else{const flap=Math.sin(gaitAngle);

  for(const side of [-1,1]){ctx.save();ctx.scale(1,1);
   ellipse(side*16,-6,19,8*(.6+.4*Math.abs(flap)),'#aea7cecc',side*(-.4-flap*.5));
   ellipse(side*13,4,13,6*(.6+.4*Math.abs(flap)),'#8f88b8cc',side*(.3+flap*.4));
   ellipse(side*17,-7,5,2.2,'#fbddf0b8',side*-.4);
   ellipse(side*22,-8-flap*2,3.4,2.5,'#766181',side*(-.4-flap*.5));
   ellipse(side*22,-8-flap*2,1.65,1.4,'#ffd7a3');ctx.restore();}
  ellipse(0,0,9,16,'#666589');ellipse(0,9,6,7,'#c9bbce');
  for(const side of [-1,1]){ellipse(side*4,-5,2,3,'#ffe2a4');glow(side*4,-5,6,'#ffe2a455');}
  line(-3,-14,-7,-20,'#b0aac6',1);line(3,-14,7,-20,'#b0aac6',1);}
 ctx.restore();}
