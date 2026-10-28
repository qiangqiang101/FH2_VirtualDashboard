#pragma once
// Dashboard page served by TelemetryDash (split in chunks: MSVC limits literal size).
static const std::string kDashboardHtml =
    std::string(R"HTML(<!doctype html><html><head><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1,user-scalable=no">
<title>VirtualDashboard</title><link rel="icon" type="image/png" href="/favicon.png">
<style>
html,body{margin:0;background:#0b0e14;color:#e8edf5;font-family:system-ui,Segoe UI,sans-serif}
#wrap{max-width:1100px;margin:0 auto;padding:8px}
#gauges{display:flex;flex-wrap:wrap;justify-content:center;align-items:center;gap:4px}
canvas{width:min(46vw,380px);height:min(46vw,380px)}
#gearbox{width:min(30vw,160px);text-align:center}
#gear,#gearlbl{cursor:pointer}#gear{font-size:min(22vw,110px);font-weight:700;line-height:1}
#gearlbl{opacity:.6;letter-spacing:.2em;font-size:12px}#gear.sp{font-size:min(18vw,90px)}
#car{text-align:center;font-size:min(7vw,34px);font-weight:600;margin:6px 0 14px}
table{width:100%;border-collapse:collapse;font-size:15px}
#nos{display:none;margin:0 8px 12px;text-align:center;color:#35b8ff;font-size:12px;letter-spacing:.2em}#nosbar{height:10px;background:#1d2433;border-radius:5px;margin-top:4px;overflow:hidden}#nosfill{height:100%;width:0;background:#35b8ff}
td{padding:6px 8px;border-top:1px solid #1d2433}
td:last-child{text-align:right;font-variant-numeric:tabular-nums;color:#8fd3ff}
#foot{justify-content:space-between;align-items:center;margin:14px 8px 20px;color:#ff5fa8;font-size:14px}#foot a{color:#ff5fa8}#lt{text-align:center;color:#3a4252;margin-top:6px}#ind{display:flex;justify-content:center;gap:16px;color:#3a4252;margin-bottom:4px}#ind svg.on{color:#7fd13b}#rawbtn{text-align:center}#tog{background:none;border:0;color:#8a96a8;padding:4px 14px}#tog svg{transition:transform .2s}#tog.open svg{transform:rotate(180deg)}#lt.on{color:#7fd13b}#status{text-align:center;font-size:12px;opacity:.6;margin-top:10px}
@media (max-width:700px){#gauges{flex-direction:row}canvas{width:48vw;height:48vw}#gearbox{width:100%;order:3}}@media (orientation:landscape) and (max-height:600px){#gauges{flex-wrap:nowrap}canvas{width:min(34vw,66vh);height:min(34vw,66vh)}#gearbox{width:24vw;order:0}#gear,#gearlbl{cursor:pointer}#gear{font-size:min(14vw,34vh)}#gear.sp{font-size:min(11.5vw,28vh)}#car{font-size:min(4vw,6vh);margin:2px 0 6px}}
</style></head><body><div id="wrap">
<div id="gauges"><canvas id="spd" width="600" height="600"></canvas>
<div id="gearbox"><div id="ind"><svg id="indL" viewBox="0 0 100 100" width="34" height="34"><path d="M5 50L48 8V32H95V68H48V92Z" fill="currentColor"/></svg><svg id="indR" viewBox="0 0 100 100" width="34" height="34"><path d="M95 50L52 8V32H5V68H52V92Z" fill="currentColor"/></svg></div><div id="gear">-</div><div id="gearlbl">GEAR</div><div id="lt"><svg id="lights" viewBox="0 0 768 768" width="40" height="40"><g fill="none" stroke="currentColor" stroke-width="38" stroke-linecap="round" stroke-linejoin="round"><path d="M410 176C480 170 560 190 640 235C735 290 745 400 640 455C560 500 480 520 410 514C370 430 370 260 410 176Z"/><path d="M300 186L35 284M300 264L35 362M300 341L35 439M300 418L35 516M300 496L35 593"/></g></svg></div></div>
<canvas id="rpm" width="600" height="600"></canvas></div>
<div id="car">Waiting for the game...</div>
<div id="nos">NOS <span id="nosv"></span><div id="nosbar"><div id="nosfill"></div></div></div>

<div id="rawbtn"><button id="tog" aria-label="Show raw values"><svg viewBox="0 0 24 24" width="26" height="26"><path d="M5 9l7 7 7-7" fill="none" stroke="currentColor" stroke-width="2.5" stroke-linecap="round" stroke-linejoin="round"/></svg></button></div>
<table id="raw" style="display:none"></table>
<div id="foot" style="display:none"><span>Created by suderek</span><span>v1.1.0</span><a href="https://fh2mods.com/" target="_blank" rel="noopener">Visit FH2mods</a></div>
<div id="status"></div></div>
<script>
)HTML") +
    std::string(R"HTML(
var d=null,mph=false,spdMax=300,ok=false,lastCar='',lastLim=0,testStart=0;
var testDown=false;
function testLevel(){
 if(!testStart)return -1;
 var e=(Date.now()-testStart)/1000,T=0.9;testDown=e>=T;
 if(e>=2*T){testStart=0;return -1;}
 var p=e<T?e/T:2-e/T;return p*p*(3-2*p);
}
document.getElementById('tog').onclick=function(){var t=document.getElementById('raw'),o=t.style.display==='none';t.style.display=o?'table':'none';document.getElementById('foot').style.display=o?'flex':'none';this.className=o?'open':'';};
document.getElementById('spd').onclick=function(){mph=!mph;};
document.getElementById('rpm').onclick=function(){rpmQ=!rpmQ;};
document.getElementById('gear').onclick=document.getElementById('gearlbl').onclick=function(){gearSpd=!gearSpd;};
function dial(cv,val,max,step,label,unit,red,fmt){
 var tl=testLevel();if(tl>=0)val=testDown?val+(max-val)*tl:max*tl;
 var c=cv.getContext('2d'),W=cv.width,cx=W/2,cy=W/2,r=W*0.42;
 c.clearRect(0,0,W,W);
 var a0=Math.PI*0.75,sw=Math.PI*1.5;
 c.lineWidth=W*0.04;c.lineCap='butt';
 c.strokeStyle='#1d2433';c.beginPath();c.arc(cx,cy,r,a0,a0+sw);c.stroke();
 if(red>0&&red<max){c.strokeStyle='#7a1f2b';c.beginPath();c.arc(cx,cy,r,a0+sw*red/max,a0+sw);c.stroke();}
 var f=Math.max(0,Math.min(1,val/max));
 c.strokeStyle=(red>0&&val>=red)?'#ff4d5e':'#35b8ff';
 c.beginPath();c.arc(cx,cy,r,a0,a0+sw*f);c.stroke();
 c.fillStyle='#cfd8e6';c.font=(W*0.05)+'px sans-serif';c.textAlign='center';c.textBaseline='middle';
 for(var v=0;v<=max+0.001;v+=step){
  var a=a0+sw*v/max,x=cx+Math.cos(a)*(r-W*0.08),y=cy+Math.sin(a)*(r-W*0.08);
  c.fillText(fmt?fmt(v):v,x,y);
 }
 var a=a0+sw*f;c.strokeStyle='#fff';c.lineWidth=W*0.012;c.beginPath();
 c.moveTo(cx,cy);c.lineTo(cx+Math.cos(a)*(r-W*0.14),cy+Math.sin(a)*(r-W*0.14));c.stroke();
 c.fillStyle='#fff';c.beginPath();c.arc(cx,cy,W*0.03,0,7);c.fill();
 c.font='bold '+(W*0.13)+'px sans-serif';c.fillText((cv.id==='spd'&&gearSpd)?gearTxt:(cv.id==='rpm'&&rpmQ)?Math.round(val/50)*50:Math.round(val),cx,cy+W*0.2);
 c.font=(W*0.05)+'px sans-serif';c.fillStyle='#8a96a8';c.fillText(unit,cx,cy+W*0.3);
}
function pretty(n){
 if(!n)return '';
 var m=n.match(/^(.*)_(\d\d)$/);
 var s=(m?m[1]:n).replace(/_/g,' ');
 return m?s+" '"+m[2]:s;
}
)HTML") +
    std::string(R"HTML(
var rpmQ=true,gearSpd=false,gearTxt='-';
function draw(){
 var v=d&&d.valid?d:{speed:0,rpm:0,redline:6500,limiter:8000,gear:0};
 var k=mph?2.2369363:3.6,sp=v.speed*k;
 var g=v.gear;gearTxt=!ok||!v.valid?'-':(g<=0?'R':String(g));
 while(sp>spdMax&&testLevel()<0)spdMax+=50;
 dial(document.getElementById('spd'),sp,spdMax,spdMax>400?100:50,'',gearSpd?'GEAR':(mph?'MPH':'KM/H'),0);
 var lim=v.limiter||8000,rm=Math.ceil(lim/1000)*1000;
 dial(document.getElementById('rpm'),v.rpm,rm,1000,'','rpm',(v.redline>0&&v.redline<lim)?v.redline:lim*0.93,function(x){return x/1000;});
 document.querySelector('#spd').title=mph?'mph':'km/h';
 var ge=document.getElementById('gear');
 ge.textContent=gearSpd?(!ok||!v.valid?'-':Math.round(sp)):gearTxt;ge.style.fontSize='';ge.className=gearSpd?'sp':'';
 document.getElementById('gearlbl').textContent=gearSpd?(mph?'MPH':'KM/H'):'GEAR';
}
function rows(v){
 var t=document.getElementById('raw'),h='';
 function r(a,b){h+='<tr><td>'+a+'</td><td>'+b+'</td></tr>';}
 if(!v||!v.valid){t.innerHTML='';return;}
 r('Car (media name)',v.car);
 r('Speed (m/s)',v.speed.toFixed(2));
 r('Speed ('+(mph?'mph':'km/h')+' - tap speedometer to switch)',(v.speed*(mph?2.2369363:3.6)).toFixed(1));
 r('Engine speed (rpm)',v.rpm.toFixed(0));
 r('Red zone starts (rpm)',v.redline.toFixed(0));
 r('Rev limiter (rpm)',v.limiter.toFixed(0));
 r('Gear (raw)',v.gear);
 r('Throttle (0-1)',v.throttle.toFixed(3));
 r('Brake (0-1)',v.brake.toFixed(3));
 r('Clutch (0-1)',v.clutch.toFixed(3));
 r('Handbrake (0-1)',v.handbrake.toFixed(3));
 r('Engine torque (Nm)',v.torque.toFixed(1));
 r('Engine power (kW)',v.power.toFixed(1));
 r('Turbo boost (bar)',((v.boost-14.696)/14.5038).toFixed(2));
 if(v.nitro>=0)r('Nitro (%)',v.nitro);
 if(v.time>=0){var tm=Math.floor(v.time/60);r('Game time',('0'+Math.floor(tm/60)).slice(-2)+':'+('0'+tm%60).slice(-2));}
 r('Weather: atmosphere / rain / environment',v.w0.toFixed(3)+' / '+v.w1.toFixed(3)+' / '+v.w2.toFixed(3));
 r('Indicators (1=L 2=R 3=hazard)',v.ind);
 r('Steering (-1..1)',v.steer.toFixed(3));
 r('Headlights (0/1)',v.lights);
 r('World position X',v.x.toFixed(2));
 r('World position Y (height)',v.y.toFixed(2));
 r('World position Z',v.z.toFixed(2));
 t.innerHTML=h;
}
function nos(j){var n=j.valid&&j.nitro>=0;document.getElementById('nos').style.display=n?'block':'none';if(n){document.getElementById('nosv').textContent=j.nitro+'%';document.getElementById('nosfill').style.width=j.nitro+'%';}}
function poll(){
 fetch('/api',{cache:'no-store'}).then(function(r){return r.json();}).then(function(j){
  d=j;ok=true;nos(j);
  if(j.valid&&(j.car!==lastCar||j.limiter!==lastLim)){lastCar=j.car;lastLim=j.limiter;spdMax=300;testStart=Date.now();}
  document.getElementById('car').textContent=j.valid?pretty(j.car):'Waiting for a car in the game...';
  document.getElementById('status').textContent='Connected';
  document.getElementById('lt').className=(j.valid&&j.lights)?'on':'';var ib=j.valid?(j.ind|0):0;document.getElementById('indL').setAttribute('class',((ib&1)&&(Date.now()%600<300))?'on':'');document.getElementById('indR').setAttribute('class',((ib&2)&&(Date.now()%600<300))?'on':'');rows(j);draw();setTimeout(poll,60);
 }).catch(function(){
  ok=false;d=null;nos({});document.getElementById('status').textContent='Connection lost, retrying...';
  document.getElementById('car').textContent='Waiting for a car in the game...';
  document.getElementById('lt').className='';document.getElementById('indL').setAttribute('class','');document.getElementById('indR').setAttribute('class','');rows(null);
  draw();setTimeout(poll,1000);
 });
}
draw();poll();setInterval(draw,33);
</script></body></html>
)HTML");
