#include "midicontrol/web_page.hpp"

namespace mc::web {

namespace {

// Field and counter names are the ones CommHandler / main write into the
// StatusRegistry (see status_json.hpp for the JSON shape).
const char* const kPage = R"HTML(<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIDIControl status</title>
<style>
:root{--bg:#fff;--fg:#1d1d1f;--line:rgba(128,128,128,.35);--muted:.62;
  --green:#2e9e4f;--yellow:#d39b00;--red:#c83a3a;--grey:#8a8a8a}
@media (prefers-color-scheme: dark){:root{--bg:#18191b;--fg:#e4e4e6;--line:rgba(160,160,160,.3);
  --green:#3fb862;--yellow:#e0ad1a;--red:#e05252;--grey:#7c7c7c}}
*{box-sizing:border-box}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.45 system-ui,-apple-system,"Segoe UI",sans-serif}
.top{display:flex;justify-content:space-between;align-items:center;gap:12px;padding:10px 16px;border-bottom:1px solid var(--line)}
.top .t{font-weight:600}.top .u{opacity:var(--muted);font-size:12px}
nav{display:flex;flex-wrap:wrap;border-bottom:1px solid var(--line);padding:0 8px}
nav a{padding:8px 12px;cursor:pointer;opacity:var(--muted);border-bottom:2px solid transparent;color:inherit;text-decoration:none}
nav a.on{opacity:1;border-bottom-color:currentColor;font-weight:600}
main{padding:12px 16px;max-width:1100px}
section{display:none}section.on{display:block}
h5{margin:16px 0 4px;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:.06em;opacity:var(--muted)}
h5:first-child{margin-top:0}
table{border-collapse:collapse;width:100%}
td{padding:2px 10px 2px 0;vertical-align:top}
.kv td:first-child{opacity:var(--muted);width:180px;white-space:nowrap}
.mono,.ev td{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:12px}
.ev td{padding:1px 12px 1px 0}.ev td:first-child{opacity:var(--muted);white-space:nowrap;width:1%}
.ev td:nth-child(2),.ev td:nth-child(3){white-space:nowrap;width:1%}
.ov td:first-child{width:130px;font-weight:600;white-space:nowrap}
.d{display:inline-block;width:8px;height:8px;border-radius:50%;margin-right:6px;vertical-align:middle;position:relative;top:-1px}
.green{background:var(--green)}.yellow{background:var(--yellow)}.red{background:var(--red)}.grey{background:var(--grey)}
.st span.i{margin-right:18px;white-space:nowrap}
.note{opacity:.5;font-style:italic}
.warn{color:var(--yellow)}
.empty{opacity:var(--muted)}
.dg select,.dg input{font:inherit;color:inherit;background:var(--bg);border:1px solid var(--line);border-radius:4px;padding:2px 6px;margin-right:8px}
.dg button{font:inherit;cursor:pointer;margin-left:10px}
.dg .fl td:first-child{opacity:var(--muted);width:160px;white-space:nowrap}
.dg .E{color:var(--red)}.dg .W{color:var(--yellow)}.dg .D{opacity:var(--muted)}
.dg .unseen{color:var(--yellow);font-weight:600}
</style>
</head>
<body>
<div class="top"><span class="t">MIDIControl</span><span class="u" id="upd">connecting…</span></div>
<nav id="tabs"></nav>
<main id="main"></main>
<script>
"use strict";
const TABS=[["overview","Overview"],["software","Software"],["base","Base"],["controller","Controller"],["diag","Diagnostics"]];
let data=null, fetchedAt=0, failed=false;

function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=String(text);return e;}
function dot(color){return el("span","d "+color);}
function pad(n,w){return String(n).padStart(w||2,"0");}
// "Now" on the server's clock, so ages don't depend on the viewer's clock.
function serverNow(){return data.now_ms+(Date.now()-fetchedAt);}
function dur(ms){
  let s=Math.max(0,Math.floor(ms/1000));
  if(s<60)return s+" s";
  if(s<3600)return Math.floor(s/60)+" min";
  const d=Math.floor(s/86400),h=Math.floor(s%86400/3600),m=Math.floor(s%3600/60);
  return (d?d+" d ":"")+pad(h)+":"+pad(m);
}
function ago(ms){if(ms==null)return null;const a=serverNow()-ms;return a<10000?(a/1000).toFixed(1)+" s ago":dur(a)+" ago";}
function clock(ms,withMs){
  const t=new Date(ms),today=new Date(serverNow());
  let s=pad(t.getHours())+":"+pad(t.getMinutes())+":"+pad(t.getSeconds());
  if(withMs)s+="."+pad(t.getMilliseconds(),3);
  if(t.toDateString()!==today.toDateString())s=pad(t.getMonth()+1)+"-"+pad(t.getDate())+" "+s;
  return s;
}
function stamp(ms){const t=new Date(ms);return t.getFullYear()+"-"+pad(t.getMonth()+1)+"-"+pad(t.getDate())+" "+pad(t.getHours())+":"+pad(t.getMinutes());}
function f(node,key){const v=data.nodes[node].fields[key];return v===undefined?null:v;}
function c(node,key){return data.nodes[node].counters[key]||0;}
function yesno(v){return v==null?"?":(v?"✔":"✘");}

// --- the four blocks, same on every node tab ---
function blockStatus(items){
  const d=el("div","st");
  for(const [color,text] of items){const s=el("span","i");s.append(dot(color),document.createTextNode(text));d.append(s);}
  return d;
}
function blockTable(rows){
  const t=el("table","kv");
  for(const r of rows){
    const tr=el("tr"),k=el("td",r.note?"note":null,r.label),v=el("td",r.note?"note":(r.mono?"mono":null));
    if(r.node)v.append(r.node);else v.textContent=r.value==null||r.value===""?"—":r.value;
    tr.append(k,v);t.append(tr);
  }
  return t;
}
function blockEvents(events,withNode){
  if(!events.length)return el("div","empty","No events yet.");
  const t=el("table","ev");
  for(const e of events){
    const tr=el("tr");let kind=e.level,text=e.text;
    const m=/^(TX|RX) (.*)$/.exec(text);
    if(e.level==="DEBUG"&&m){kind=m[1];text=m[2];}
    tr.append(el("td",null,clock(e.t,!withNode)),el("td",null,kind));
    if(withNode)tr.append(el("td",null,TABS.find(x=>x[0]===e.node)[1]));
    tr.append(el("td",null,text));t.append(tr);
  }
  return t;
}
function nodeTab(status,details,counters,events){
  const s=el("section");
  s.append(el("h5",null,"Status"),blockStatus(status),el("h5",null,"Details"),blockTable(details),
           el("h5",null,"Counters"),blockTable(counters),el("h5",null,"Recent events"),blockEvents(events,false));
  return s;
}
function packetCounters(n){
  return [{label:"Packets tx / rx",value:c(n,"tx")+" / "+c(n,"rx")},
          {label:"Retries / gave up",value:c(n,"retries")+" / "+c(n,"gave_up")},
          {label:"NAK sent / received",value:c(n,"nak_sent")+" / "+c(n,"nak_received")}];
}

// --- per node ---
function playbackText(){
  const p=f("software","playback")||"idle";
  if(p!=="idle"){const sec=f("software","section");return p+" "+(f("software","song")||"")+(sec?", "+sec:"");}
  const last=f("software","last_song");
  if(!last)return "idle";
  const secs=f("software","last_sections");
  return "idle · last "+last+(secs?" ("+secs+")":"")+" "+(f("software","last_end")||"");
}
function softwareSummary(){
  const st=f("software","started_ms");
  let s="Running"+(st!=null?" "+dur(serverNow()-st):"")+" · "+playbackText();
  const act=f("software","activity");if(act)s+=" · "+act;
  return s;
}
function software(){
  const n="software",items=[[data.nodes[n].dot==="red"?"red":"green","Running"]];
  const p=f(n,"playback")||"idle";
  items.push(p==="idle"?["grey","Idle"]:["green",(p==="playing"?"Playing ":"Warming up ")+(f(n,"song")||"")+(f(n,"section")?" "+f(n,"section"):"")]);
  const sp=f(n,"song_problems")||0;
  if(sp)items.push(["yellow",sp+" song file warning"+(sp>1?"s":"")]);
  if(f(n,"activity"))items.push(["yellow",f(n,"activity")]);
  const songs=el("span","mono");
  for(const s of data.songs){
    const x=el("span",s.problem?"warn":null,s.code+(s.problem?"⚠":""));
    if(s.problem)x.title=s.problem;
    songs.append(x,document.createTextNode(" "));
  }
  const st=f(n,"started_ms"),ver=f(n,"version");
  const details=[
    {label:"Version",value:ver?ver+" (built "+(f(n,"build_date")||"?")+")":null,mono:true},
    {label:"Started",value:st!=null?stamp(st)+" · uptime "+dur(serverNow()-st):null},
    {label:"Config",value:f(n,"config_path"),mono:true},
    {label:"Play folder",value:f(n,"play_folder")!=null?f(n,"play_folder")+" · "+(f(n,"song_count")||0)+" songs":null,mono:true},
    {label:"Playback",value:playbackText()},
    data.songs.length?{label:"Songs",node:songs}:{label:"Songs",value:null}];
  for(const s of data.songs)if(s.problem)details.push({label:"Song "+s.code,value:s.problem});
  const ended=c(n,"songs_ended"),stopped=c(n,"songs_stopped");
  const counters=[{label:"Songs played",value:(ended+stopped)+" ("+ended+" ended, "+stopped+" stopped)"},
                  {label:"Warnings / errors",value:c(n,"warnings")+" / "+c(n,"errors")}];
  return nodeTab(items,details,counters,data.nodes[n].events.slice());
}
function baseSummary(){
  const n="base";
  if(f(n,"updating"))return "Firmware update in progress";
  if(!f(n,"connected"))return "Waiting for the base (not connected)";
  let s="Connected "+(f(n,"device")||"");
  s+=f(n,"alive")===false?" · keepalives lost":" · keepalive "+(ago(f(n,"keepalive_ack_ms"))||"never");
  s+=" · organ power "+(f(n,"organ_power")?"on":"off");
  if(f(n,"status_answered")===false)s+=" · GET_STATUS not answered";
  return s;
}
function base(){
  const n="base",conn=f(n,"connected"),items=[];
  if(f(n,"updating"))items.push(["yellow","Firmware update in progress"]);
  else if(!conn)items.push(["yellow","Waiting for the base"]);
  else if(f(n,"alive")===false)items.push(["red","Serial open, keepalives lost"]);
  else items.push(["green","Connected"]);
  const mp=f(n,"midi_port_found");
  items.push(mp==null?["grey","MIDI port unknown"]:(mp?["green","MIDI port"]:["yellow","No MIDI port"]));
  items.push(f(n,"organ_power")?["green","Organ power on"]:["grey","Organ power off"]);
  if(conn&&f(n,"status_answered")===false)items.push(["yellow","GET_STATUS not answered"]);
  const sm=f(n,"status_ms");
  let reports=null;
  if(sm!=null)reports="BLE "+yesno(f(n,"status_ble"))+" · relay "+yesno(f(n,"status_relay"))+" · display "+
     yesno(f(n,"status_display"))+" · PC "+yesno(f(n,"status_pc"))+" (GET_STATUS, "+ago(sm)+")";
  if(f(n,"status_answered")===false)reports=(reports?reports+" · ":"")+"last request not answered";
  const since=f(n,"connected_since_ms");
  const details=[
    {label:"Serial port",value:f(n,"device")!=null?f(n,"device")+(f(n,"vidpid")?" · "+f(n,"vidpid"):""):null,mono:true},
    {label:"MIDI out",value:f(n,"midi_out"),mono:true},
    {label:"Last keepalive ACK",value:ago(f(n,"keepalive_ack_ms"))},
    {label:"Connected since",value:(conn&&since!=null?stamp(since):"not connected")+" (reconnects: "+(f(n,"reconnects")||0)+")"},
    {label:"Base reports",value:reports},
    {label:"Firmware debug",value:"later: version, uptime, reset cause…",note:true}];
  const counters=packetCounters(n);
  counters.push({label:"CRC errors / duplicates",value:c(n,"crc_errors")+" / "+c(n,"duplicates")});
  return nodeTab(items,details,counters,data.nodes[n].events.slice());
}
function controllerStatus(){
  const d=data.nodes.controller.dot;
  if(d==="green")return [d,"BLE connected (per base)"];
  if(d==="yellow")return [d,"BLE disconnected (per base)"];
  return [d,f("base","connected")?"No fresh report from the base":"Base not connected"];
}
function controllerSummary(){
  const heard=ago(f("controller","last_rx_ms"));
  return controllerStatus()[1]+" · "+(heard?"last heard "+heard:"never heard");
}
function controller(){
  const n="controller",heard=ago(f(n,"last_rx_ms"));
  const items=[controllerStatus(),["grey",heard?"Last heard "+heard:"Never heard"]];
  const cmd=f(n,"last_cmd"),tx=f(n,"last_tx");
  const details=[
    {label:"Route",value:"via base, BLE"+(f("base","status_ms")!=null?" (base reports BLE "+yesno(f("base","status_ble"))+")":"")},
    {label:"Last command",value:cmd?cmd+" at "+clock(f(n,"last_cmd_ms"))+" → "+(f(n,"last_cmd_reply")||"?"):null},
    {label:"Last sent to it",value:tx?tx+" at "+clock(f(n,"last_tx_ms"))+" · "+(f(n,"last_tx_result")||"?"):null},
    {label:"Firmware debug",value:"later: battery, version, uptime…",note:true}];
  const counters=packetCounters(n),cmds=[];
  for(const [k,v] of Object.entries(data.nodes[n].counters))if(k.startsWith("cmd."))cmds.push(k.slice(4)+" "+v);
  counters.push({label:"Commands",value:cmds.length?cmds.join(" · "):"none"});
  return nodeTab(items,details,counters,data.nodes[n].events.slice());
}
function overview(){
  const s=el("section"),t=el("table","ov");
  const rows=[["software","Software",softwareSummary()],["base","Base",baseSummary()],["controller","Controller",controllerSummary()]];
  for(const [k,name,text] of rows){
    const tr=el("tr"),a=el("td");a.append(dot(data.nodes[k].dot),document.createTextNode(name));
    tr.append(a,el("td",null,text));t.append(tr);
  }
  s.append(t,el("h5",null,"Problems"),blockEvents(data.problems,true));
  return s;
}

// --- Diagnostics tab: built once (the filter inputs must keep focus), filled by its own 1 s poll ---
const DG={sec:null,body:null,faults:null,resets:null,note:null,entries:[],resetEntries:[],node:"",level:"D",tag:"",text:"",timer:null};
function hex(v){return "0x"+(v>>>0).toString(16).padStart(8,"0");}
function faultTable(name,node,fl,unseen){
  const box=el("div");
  const h=el("h5",null,name+" – last fault");
  box.append(h);
  if(!fl){box.append(el("div","empty","no fault recorded"));return box;}
  const f=fl.fault||{},bits=f.cfsr_bits||[];
  const ipsr=(f.xpsr>>>0)&0x1FF;
  const rows=[["Kind",f.kind+(f.count?" #"+f.count:"")],["When",stamp(fl.t)+(unseen>0?"  (not marked seen)":"  (seen)")],
    ["pc / lr",hex(f.pc)+" / "+hex(f.lr)],
    ["Interrupted",ipsr===0?"thread mode":"exception "+ipsr+"  (xpsr "+hex(f.xpsr)+")"]];
  if(f.cfsr!==undefined)rows.push(["CFSR",hex(f.cfsr)+"  "+bits.join(" ")]);
  if(f.hfsr)rows.push(["HFSR",hex(f.hfsr)+"  "+(f.hfsr_bits||[]).join(" ")]);
  if(bits.includes("BFARVALID"))rows.push(["BFAR",hex(f.bfar)]);
  if(bits.includes("MMARVALID"))rows.push(["MMFAR",hex(f.mmfar)]);
  if(f.rev)rows.push(["Revision",f.rev]);
  if(f.up)rows.push(["Uptime at fault",dur(f.up)]);
  const t=el("table","fl mono");
  for(const [k,v] of rows){const tr=el("tr");tr.append(el("td",null,k),el("td",unseen>0&&k==="When"?"unseen":null,v));t.append(tr);}
  box.append(t);
  if(unseen>0){
    const b=el("button",null,"Mark seen");
    b.onclick=async()=>{b.disabled=true;try{await fetch("/api/diag?limit=1&ack="+fl.id,{cache:"no-store"});}catch(e){}pollOnce();};
    h.append(b);
  }
  return box;
}
function resetText(e){
  // base banner "reset: IWDG (csr ...)", remote BOOT record "boot, reset: PIN|SOFTWARE"
  return e.text.startsWith("reset:")||e.text.startsWith("boot, reset")?e.text:null;
}
function drawDiag(){
  if(!DG.sec)return;
  DG.faults.replaceChildren();
  if(data){
    DG.faults.append(faultTable("Base","base",data.nodes.base.last_fault,data.nodes.base.unseen_faults),
                     faultTable("Remote","controller",data.nodes.controller.last_fault,data.nodes.controller.unseen_faults));
  }
  DG.resets.replaceChildren();
  const rt=el("table","ev");let n=0;
  for(const e of DG.resetEntries){
    const r=resetText(e);if(r===null)continue;
    const tr=el("tr");tr.append(el("td",null,stamp(e.t)),el("td",null,e.node),el("td",null,r));rt.append(tr);
    if(++n>=10)break;
  }
  DG.resets.append(n?rt:el("div","empty","none recorded"));
  const q=DG.text.toLowerCase(),tg=DG.tag.toLowerCase(),t=el("table","ev");
  let shown=0;
  for(const e of DG.entries){
    if(tg&&!e.tag.toLowerCase().includes(tg))continue;
    if(q&&!e.text.toLowerCase().includes(q))continue;
    const tr=el("tr",e.lvl);
    tr.append(el("td",null,clock(e.t,true)),el("td",null,e.node),el("td",null,e.lvl),el("td",null,e.tag),el("td",null,e.text));
    t.append(tr);shown++;
  }
  DG.body.replaceChildren(shown?t:el("div","empty","no entries"));
}
function diagSection(){
  if(DG.sec)return DG.sec;
  const s=el("section","dg");
  DG.faults=el("div");DG.resets=el("div");DG.body=el("div");
  const bar=el("div");
  const node=el("select"),level=el("select"),tag=el("input"),text=el("input");
  for(const [v,n] of [["","both nodes"],["base","base"],["remote","remote"]]){const o=el("option",null,n);o.value=v;node.append(o);}
  for(const [v,n] of [["D","all levels"],["I","info+"],["W","warnings+"],["E","errors"]]){const o=el("option",null,n);o.value=v;level.append(o);}
  tag.placeholder="tag";tag.size=8;text.placeholder="search text";text.size=24;
  node.onchange=()=>{DG.node=node.value;pollOnce();};
  level.onchange=()=>{DG.level=level.value;pollOnce();};
  tag.oninput=()=>{DG.tag=tag.value;drawDiag();};
  text.oninput=()=>{DG.text=text.value;drawDiag();};
  bar.append(node,level,tag,text);
  s.append(DG.faults,el("h5",null,"Reset causes"),DG.resets,el("h5",null,"Log"),bar,DG.body);
  DG.sec=s;
  return s;
}
async function getDiag(qs){
  const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),3000);
  try{
    const r=await fetch("/api/diag?"+qs,{cache:"no-store",signal:ac.signal});
    if(!r.ok)throw new Error(r.status);
    return (await r.json()).entries;
  }finally{clearTimeout(timer);}
}
async function pollOnce(){
  try{
    DG.entries=await getDiag("limit=200&level="+DG.level+(DG.node?"&node="+DG.node:""));
    DG.resetEntries=await getDiag("limit=200&level=I");
  }catch(e){}
  if(String(window.getSelection())==="")drawDiag();
}
function diagLoop(){
  DG.timer=null;
  if(current()!=="diag")return;  // polls only while the tab is open
  pollOnce().then(()=>{if(!DG.timer&&current()==="diag")DG.timer=setTimeout(diagLoop,1000);});
}
function diag(){diagSection();drawDiag();if(!DG.timer)diagLoop();return DG.sec;}

// --- page ---
function current(){const h=location.hash.slice(1);return TABS.some(x=>x[0]===h)?h:"overview";}
function renderTabs(){
  const nav=document.getElementById("tabs");nav.replaceChildren();
  for(const [k,name] of TABS){
    const a=el("a",k===current()?"on":null,name);a.href="#"+k;
    a.onclick=e=>{e.preventDefault();history.replaceState(null,"","#"+k);render();};
    nav.append(a);
  }
}
function render(){
  renderTabs();
  if(!data)return;
  const builders={overview,software,base,controller,diag};
  const sec=builders[current()]();sec.classList.add("on");
  const main=document.getElementById("main");
  if(main.firstChild!==sec)main.replaceChildren(sec);  // the cached Diagnostics section keeps its input focus
}
function renderTop(){
  const u=document.getElementById("upd");u.replaceChildren();
  if(failed)u.append(dot("red"));
  let s=location.host;
  if(data)s+=" · updated "+((Date.now()-fetchedAt)/1000).toFixed(1)+" s ago";
  if(failed)s+=" · no connection";
  u.append(document.createTextNode(s));
}
async function poll(){
  // Give up after 3 s, so a frozen service shows as "no connection".
  const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),3000);
  try{
    const r=await fetch("/api/status",{cache:"no-store",signal:ac.signal});
    if(!r.ok)throw new Error(r.status);
    data=await r.json();fetchedAt=Date.now();failed=false;
    // Don't redraw while the viewer is selecting text.
    if(String(window.getSelection())==="")render();
  }catch(e){failed=true;}
  clearTimeout(timer);
  renderTop();
  setTimeout(poll,1000);
}
window.addEventListener("hashchange",render);
setInterval(renderTop,500);
renderTabs();
poll();
</script>
</body>
</html>
)HTML";

}  // namespace

const std::string& statusPageHtml() {
    static const std::string page(kPage);
    return page;
}

}  // namespace mc::web
