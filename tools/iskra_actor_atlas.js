// Offline canvas art for the creatures and relics living in Iskra's forest.
// Each drawing uses a 64-unit square. The runtime slices the baked poses along
// the same sprite cells used by the native movement and occlusion code.
const TAU = Math.PI*2, W = 128, cam = 0;
const clamp = x => Math.max(0, Math.min(1, x));
const lerp = (a,b,t) => a+(b-a)*t;
let clock=0, gaitAngle=0, capturingFlame=false, ctx;
const flameAnchors=[];
const animations = ['Idle','Walk','Run','Jump','Fall','Swim','Climb','Crouch','Cast','Brake','Sleep',
  'Beetle','Moth','Stonebug','Thornbug','Roll','Herald','Jelly','Minnow','Thistle','Ember','Owl','Mask','Bark',
  'Seed','Heart','Prism','Lamp','Wisp','Flare','Bubble','Fern','Beacon','Spring','Bough','Shard','Petal','Amber','Warden','10','20','40','50','80','0','00','+','1'];
const FRAMES=8, TILE=96;
function paintRobot(kind,n){
  const p=n/FRAMES, loop=p*TAU;
  const walk=kind==='Walk'?1:kind==='Run'?1.25:kind==='Swim'?.8:kind==='Climb'?.6:0;
  if(kind==='Walk'||kind==='Run'){ctx.translate(2.1,0);ctx.scale(.93,1);}
  // Leave room for the swinging lantern and antenna throughout each action.
  if(['Swim','Climb','Cast','Sleep','Brake'].includes(kind)){
    const factor=kind==='Swim'?.78:kind==='Climb'||kind==='Cast'?.86:.91;
    const center=kind==='Cast'?28:kind==='Sleep'?39:32;
    ctx.translate(center,63);ctx.scale(factor,factor);ctx.translate(-32,-64);
  }
  ctx.translate(28,61);ctx.scale(.30,.211);
  if(kind==='Fall')ctx.rotate(.035+n*.012);
  if(kind==='Jump')ctx.rotate(-.14+n*.018);
  if(kind==='Swim'){ctx.translate(-9,-22);ctx.rotate(.35);}
  if(kind==='Brake')ctx.rotate(-.16);
  if(kind==='Sleep'){ctx.translate(0,-30);ctx.rotate(-.40);}
  if(kind==='Crouch'){ctx.translate(0,-2);ctx.scale(1,.65);}
  ctx.translate(0,-685);
  robot({x:0,cam:0,t:loop/8.5+(kind==='Idle'?p*2.8:0),walk,stand:kind==='Crouch'?.25:1,
    air:['Jump','Fall','Swim'].includes(kind)?1:0,kind,phase:loop,progress:n/(FRAMES-1),light:.95,look:.8,
    give:kind==='Cast'?.5+.45*Math.sin(loop/2):kind==='Climb'?.55:0,happy:0,wind:walk*.7+.1});
}
const jade='#71b5a3',gold='#ffe7a1',ink='#19323c',lilac='#bdafda';
function gem(x,y,w,h,tone){
  poly([[x,y-h],[x+w,y-1],[x+1,y+h],[x-w,y+1]],tone);
  poly([[x,y-h],[x+1,y],[x-w,y+1]],'#e2fff042');line(x-w,y+1,x,y-h,'#dcece0aa',.9);
}
function stem(x,y,lean){bezier([[x,y],[x-3,y-13],[x+6,y-23],[x+lean,y-33]],'#527e74',3);
  for(let i=1;i<=3;i++){const v=i%2===0?1:-1;ellipse(x+v*5,y-i*7,7,2.8,'#91b998',v*-.52);}}
function eye(x,y,s=1){ellipse(x,y,4.3*s,5*s,ink);ellipse(x+.4*s,y,2.3*s,3*s,gold);ellipse(x,y-1.4*s,.9*s,1*s,'#fffae4');}
function legs(p,tone){
  // Alternating tripod gait: one tripod plants as the other reaches forward.
  for(const far of [true,false]){
    for(let i=0;i<3;i++){
      const a=p+i*Math.PI+(far?Math.PI:0);
      const x=17+i*14+(far?1:-2), lift=Math.max(0,-Math.sin(a));
      const toeX=x+Math.cos(a)*7+(i-1)*4;
      const toeY=(far?53:57)-lift*8;
      const kneeX=x+(i-1)*3+Math.cos(a-.4)*3;
      const kneeY=48-lift*5;
      line(x,40,kneeX,kneeY,far?'#39505a':tone,3);
      line(kneeX,kneeY,toeX,toeY,far?'#50606d':tone,2.8);
      line(toeX,toeY,toeX+3,toeY,far?'#50606d':'#c8c3b4',2);
    }
  }
}
function painted(kind,n){
 const p=n/FRAMES*TAU, s=Math.sin(p);
 const label = ['10','20','40','50','80','0','00','+','1'].includes(kind);
 if(label){ctx.textAlign='center';ctx.textBaseline='middle';ctx.font='bold 52px Georgia';
   ctx.strokeStyle='#393448';ctx.lineWidth=3;ctx.strokeText(kind,32,34,63);
   ctx.fillStyle='#ffe4ac';ctx.fillText(kind,32,34,63);return;}
 if(animations.indexOf(kind)<11){capturingFlame=true;paintRobot(kind,n);capturingFlame=false;return;}
 if(kind==='Beetle'||kind==='Moth'){
   ctx.translate(32,kind==='Moth'?32:34);ctx.scale(kind==='Moth'?.87:1.14,kind==='Moth'?1.25:1.28);
   drawEnemy({x:-24,y:-18,w:48,h:36,type:kind==='Moth'?'moth':'beetle'});return;
 }
 if(['Stonebug','Thornbug','Roll'].includes(kind)){
   const roll=kind==='Roll'; if(!roll)legs(p,'#9db6ad');
   ctx.translate(32,roll?34:35-Math.abs(Math.sin(p))*2);if(roll)ctx.rotate(p);else ctx.rotate(s*.046);
   ellipse(0,2,25,roll?22:18,ink);ellipse(0,-1,23,roll?20:16,gradient(-18,-15,20,17,[[0,'#c2c8aa'],[.48,'#748d83'],[1,'#3d535c']]));
   for(let i=0;i<5;i++){let x=(i-2)*8,y=-Math.sqrt(Math.max(0,1-x*x/380))*12;
     if(kind==='Thornbug')poly([[x-5,y+5],[x,y-12],[x+5,y+3]],'#96839c');
     bezier([[x-8,y+5],[x-2,y-1],[x+2,y-1],[x+8,y+5]],'#b9ceba',1.8);}
   if(roll){
     for(let i=0;i<4;i++){const a=i*TAU/4;
       ctx.save();ctx.rotate(a);bezier([[3,-18],[14,-17],[18,-9],[18,-5]],'#d5dcbd',2);ctx.restore();}
     gem(4,1,7,9,'#a0dbb9');
   }else{eye(9,4,.8);eye(18,3,.75);}return;
 }
 if(kind==='Herald'||kind==='Warden'){
   legs(p,'#658577');round(12,15,40,36,11,gradient(0,10,46,54,[[0,'#adb3a0'],[1,'#465867']]),ink);
   for(const side of [-1,1]){const x=32+side*14;bezier([[x,19],[x+side*7,13],[x+side*4,8],[x+side*10,3]],'#8ea68e',3);
     line(x+side*7,11,x+side*13,10,'#b4bb97',2);}
   ellipse(32,31,18,10,'#263f47');eye(24,30,.9);eye(39,30,.9);gem(32,44,4,6,jade);
   if(kind==='Herald'){line(48,50,53,14,'#967d68',3);gem(54,11,5,8,lilac);}return;
 }
 if(kind==='Jelly'||kind==='Minnow'){
   ctx.translate(31,30+s*2);if(kind==='Minnow')ctx.rotate(Math.sin(p-.6)*.07);
   if(kind==='Jelly'){
    for(let i=-2;i<=2;i++){const x=i*6;bezier([[x,5],[x+6*s,15],[x-9*s,23],[x+4*s,30-i*i]],'#b7acd8',2.4);}
    ctx.scale(1+s*.06,1-s*.10);
    const bell=gradient(-12,-17,12,16,[[0,'#ded3f3'],[.5,'#aa98d4'],[1,'#646c9d']]);
    ctx.beginPath();ctx.moveTo(-24,7);ctx.bezierCurveTo(-23,-23,16,-25,24,7);
    for(let x=24;x>-24;x-=8)ctx.quadraticCurveTo(x-4,14,x-8,7);
    ctx.fillStyle=bell;ctx.fill();ctx.strokeStyle='#555479';ctx.lineWidth=1.8;ctx.stroke();
    bezier([[-18,1],[-14,-11],[-5,-12],[-2,-13]],'#efe2fc',2);
    for(const side of [-1,1])bezier([[side*7,-11],[side*10,-8],[side*12,-1],[side*12,5]],'#e9d6f0aa',1.6);
   }else{poly([[-12,0],[-28+6*s,-14+s*6],[-25+6*s,0],[-28+6*s,13+s*5]],'#9eb5ca');
    line(-22+6*s,-6+s*2,-14,0,'#d0c6d9',1.4);
    ellipse(3,0,22,12,gradient(0,-12,0,14,[[0,'#9bc8b9'],[.6,'#606f93'],[1,'#485078']]));poly([[-4,-7],[-14,-21],[9,-11]],'#c5b2d8');}
   eye(kind==='Jelly'?-7:15,2,.65);if(kind==='Jelly')eye(7,2,.65);return;
 }
 if(kind==='Thistle'){stem(31,62,s*5);ellipse(32,23,18,15,'#484769');for(let i=0;i<8;i++){const a=i*TAU/8;
   gem(32+Math.cos(a)*18,23+Math.sin(a)*15,4,6,i%2?'#9791b7':'#c9aacd');}eye(32,22,1);return;}
 if(kind==='Owl'||kind==='Mask'||kind==='Bark'){
    if(kind==='Bark'){legs(p,'#637876');ellipse(32,31,30,20,'#3f5361');
      for(let i=0;i<4;i++)gem(6+i*14,29+(i%2)*3,10,14,i%2?'#738581':'#9caa96');return;}
    const boss=kind==='Mask',r=boss?1:.81;ctx.translate(32,35+s);ctx.scale(r,r);
    for(const d of [-1,1]){
      const root=14*d;bezier([[root,-13],[18*d,-21],[22*d,-30],[28*d,-31]],'#ab907a',3.5);
      line(22*d,-25,18*d,-33,'#c5ab91',2.2);
      ellipse(17*d,7,9,15,'#677489',-.45*d);
    }
    ellipse(0,1,22,23,gradient(-20,-22,20,21,[[0,'#c5c6ae'],[.55,'#909a9e'],[1,'#525d76']]));
    poly([[-20,-12],[-2,-4],[-7,8],[-18,2]],ink);poly([[20,-12],[2,-4],[7,8],[18,2]],ink);
    eye(-10,-2,.66);eye(10,-2,.66);gem(0,8,5,8,'#dbad78');gem(0,-15,4,5,jade);return;
 }
 if(kind==='Seed'||kind==='Heart'||kind==='Prism'||kind==='Amber'){
    ctx.translate(32,32+s*1.4);
    if(kind==='Seed'){
      ctx.rotate(.15);glow(0,1,27,'#ffc85a32');
      ellipse(0,5,16,20,gradient(-15,-10,14,25,[[0,'#f4d48e'],[.45,'#d19d64'],[1,'#835d55']]));
      bezier([[-7,16],[-13,7],[-10,-3],[-7,-8]],'#fff0b9',2.8);
      ellipse(0,-7,18,8,gradient(-14,-13,12,0,[[0,'#8ab998'],[1,'#396960']]));
      for(const x of [-9,0,9])bezier([[x-4,-9],[x,-8],[x,-5],[x+5,-5]],'#c0d0a1',1.7);
      bezier([[0,-12],[4,-19],[6,-20],[9,-23]],'#bbab7d',3.2);
      ellipse(-5,-19,9.5,4.4,'#a8d7a3',.48);
    }else if(kind==='Heart'){
      glow(0,0,27,'#ff8dba44');
      ctx.beginPath();ctx.moveTo(0,22);ctx.bezierCurveTo(-7,15,-21,6,-20,-6);
      ctx.bezierCurveTo(-19,-20,-6,-23,0,-12);ctx.bezierCurveTo(9,-23,22,-17,20,-5);
      ctx.bezierCurveTo(19,7,7,17,0,22);ctx.fillStyle=gradient(-18,-20,17,21,[[0,'#ffdbce'],[.45,'#e591a0'],[1,'#925476']]);ctx.fill();
      ctx.strokeStyle='#754862';ctx.lineWidth=2;ctx.stroke();ellipse(-10,-9,4.8,2.8,'#fff0d9',-.55);
      bezier([[0,18],[2,7],[11,2],[14,-7]],'#f9c4b4',1.8);
      ellipse(4,-19,8,3.5,'#9bc6a6',-.52);
    }else if(kind==='Prism'){
      glow(0,0,29,'#94fce666');
      const outline=[[0,-24],[15,-9],[12,12],[0,24],[-13,12],[-16,-9]];
      poly(outline,'#5d8b9e');poly([[0,-22],[5,-7],[3,11],[0,21],[-7,9],[-6,-7]],'#b4f6de');
      poly([[0,-22],[-6,-7],[-13,-8]],'#f3fff1');poly([[5,-7],[13,-8],[10,11],[3,11]],'#75c0c6');
      line(-12,-8,-8,-13,'#e2fff7',2);line(3,10,0,18,'#e7ffe5',1.8);
    }else{spark(0,0,14,.98,n,false);}
    return;
 }
 if(kind==='Lamp'||kind==='Beacon'){ctx.translate(32,kind==='Lamp'?9:4);ctx.scale(.95,.72);lantern(0,0,s*.09,1);return;}
 if(kind==='Spring'){for(let i=0;i<3;i++)bezier([[7,48-i*10],[18,32-i*8],[41,57-i*10],[57,34-i*10]],'#97b7a1',3.8);
   round(2,6,60,9,4,'#c5ccbd');round(4,51,56,8,4,'#6a9188');return;}
 if(kind==='Bough'){round(0,6,64,46,3,gradient(0,7,0,52,[[0,'#b4b499'],[.19,'#789477'],[.32,'#465865'],[1,'#263845']]));
   for(let i=0;i<4;i++){const x=i*16;bezier([[x,28],[x+7,24],[x+9,36],[x+16,33]],'#77928b',2);}
   line(0,10,64,10,'#d2cfab',3);return;}
 if(kind==='Fern'){stem(31,64,0);stem(35,35,1);return;}
 if(kind==='Bubble'){ellipse(32,32,23,23,'#afc6e32d');ctx.beginPath();ctx.arc(32,32,22,2.8,5.4);ctx.strokeStyle='#b9e8e3';ctx.lineWidth=2.8;ctx.stroke();ellipse(24,20,5,3,'#e7f1ec');return;}
 if(kind==='Shard'){ctx.translate(32,32);ctx.rotate(p);gem(0,0,13,25,'#979189');gem(6,-4,5,9,'#c8cba2');return;}
 if(kind==='Petal'){ctx.translate(32,32);ctx.rotate(p);ellipse(0,0,12,24,lilac,.3);line(-4,16,4,-16,'#daf1d9',2);return;}
 if(kind==='Wisp'||kind==='Ember'||kind==='Flare'){
    ctx.translate(32,32);const radius=kind==='Flare'?16+5*s:kind==='Ember'?17:12;
    glow(0,0,32,kind==='Wisp'?'#92ffdf99':'#ffaa5977',1);
    for(let i=0;i<4;i++){const a=i*TAU/4+p;ellipse(Math.cos(a)*radius*.65,Math.sin(a)*radius*.65,radius*.50,radius*.85,kind==='Wisp'?'#a3e9da99':'#eda68eaa',a);}
    gem(0,0,radius*.73,radius,kind==='Wisp'?'#baffeb':gold);return;
 }
}
// Fit the whole animation strip together. A reach at either end of a stride
// must not crop the glove or change the body scale between adjacent frames.
function bakeHeroRow(sheet,kind,row) {
  const pad=32, size=TILE+2*pad;
  let left=0,right=TILE,top=0,bottom=TILE;
  const poses=[];
  for(let n=0;n<FRAMES;n++){
    const pose=makeCanvas(size,size);ctx=pose.getContext('2d');
    ctx.translate(pad,pad);ctx.scale(TILE/64,TILE/64);
    gaitAngle=n/FRAMES*TAU;clock=n*.091+row*.127;painted(kind,n);
    const pixels=ctx.getImageData(0,0,size,size).data;
    for(let y=0;y<size;y++)for(let x=0;x<size;x++){
      if(pixels[(y*size+x)*4+3]<180)continue;
      left=Math.min(left,x-pad-2);right=Math.max(right,x-pad+3);
      top=Math.min(top,y-pad-2);bottom=Math.max(bottom,y-pad+3);
    }
    poses.push(pose);
  }
  const sx=TILE/(right-left),sy=TILE/(bottom-top);
  for(let n=0;n<FRAMES;n++){
    const at=row*FRAMES+n;
    flameAnchors[at]=[Math.round((flameAnchors[at][0]-pad-left)*sx),
                      Math.round((flameAnchors[at][1]-pad-top)*sy)];
    sheet.drawImage(poses[n],pad+left,pad+top,right-left,bottom-top,n*TILE,row*TILE,TILE,TILE);
  }
}
function bakeAtlas(){
  const atlas=makeCanvas(FRAMES*TILE,animations.length*TILE), sheet=atlas.getContext('2d');
  animations.forEach((kind,row)=>{
    if(row<11){bakeHeroRow(sheet,kind,row);return;}
    ctx=sheet;
    for(let n=0;n<FRAMES;n++){
      gaitAngle=n/FRAMES*TAU;clock=n*.091+row*.127;ctx.save();ctx.translate(n*TILE,row*TILE);
      ctx.beginPath();ctx.rect(0,0,TILE,TILE);ctx.clip();ctx.scale(TILE/64,TILE/64);
      painted(kind,n);ctx.restore();
    }
  });
  return atlas.toDataURL('image/png').split(',')[1];
}
