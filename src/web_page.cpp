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
/* Stops tab: the panel keeps its own dark wooden colours in both themes */
.sp [hidden]{display:none}
.sp-head{display:flex;gap:12px;align-items:baseline;flex-wrap:wrap;margin:0 0 10px}
.sp-cur{font-weight:700;font-size:20px}
.sp-count{opacity:.7}
.sp-state{font-size:13px;padding:2px 8px;border-radius:10px;background:rgba(128,128,128,.2)}
.sp-state.dirty{background:#e0a800;color:#000}
.sp-err{color:var(--red)}
.sp-panel{display:flex;flex-wrap:wrap;gap:12px 40px;padding:18px;border-radius:10px;background:#3b2a1e}
.sp-col{display:flex;flex-direction:column;gap:8px}
.sp-glabel{background:linear-gradient(#f3d77a,#c99a2e);color:#000;font-weight:700;font-size:13px;letter-spacing:.04em;text-align:center;padding:4px 8px;border-radius:3px;
  box-shadow:inset 0 1px 0 rgba(255,255,255,.5),0 1px 2px rgba(0,0,0,.5);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sp-group{display:flex;gap:5px}
.sp-tab{appearance:none;border:0;margin:0;padding:0;font:inherit;width:44px;height:132px;border-radius:5px;position:relative;cursor:pointer;user-select:none;
  background:#cfc6b4;color:#5a4a3a;box-shadow:inset 0 0 0 1px rgba(0,0,0,.3);transition:background .1s,color .1s}
.sp-tab[aria-pressed="true"]{background:#fff4d0;color:#1d140b;box-shadow:inset 0 0 0 3px #e0a800,0 0 10px rgba(255,200,60,.55)}
.sp-tab:focus-visible{outline:2px solid #f3d77a;outline-offset:2px}
.sp-num{position:absolute;top:30%;left:0;right:0;transform:translateY(-50%);text-align:center;font-weight:700;font-size:17px}
.sp-name{position:absolute;top:45%;left:2px;right:2px;text-align:center;font-weight:600;font-size:9px;line-height:1.15;opacity:.85}  /* wraps at spaces; spFit shrinks long words */
.sp-panel.notset .sp-tab{opacity:.45}
.sp-actions{margin:12px 0 22px;display:flex;flex-wrap:wrap;gap:10px;align-items:center;max-width:720px}
.sp-actions button{font:inherit;font-size:15px;padding:8px 16px;cursor:pointer}
.sp-actions button.sp-small{font-size:13px;padding:4px 10px}
.sp-actions button:disabled{opacity:.4;cursor:default}
.sp-msg{flex:1 1 12em}  /* also pushes "Not set" to the right */
.sp-msg:empty{flex-basis:0}
.sp-msg.err{color:var(--red)}
table.sp-songs{max-width:720px}
.sp-songs th,.sp-songs td{padding:8px 10px;text-align:left;vertical-align:baseline;border-bottom:1px solid var(--line)}
.sp-songs th button{appearance:none;border:0;padding:0;background:none;color:inherit;font:inherit;font-size:13px;font-weight:600;text-transform:uppercase;opacity:.75;cursor:pointer}
.sp-songs th[aria-sort="ascending"] button::after{content:" ▲"}
.sp-songs th[aria-sort="descending"] button::after{content:" ▼"}
.sp-row{cursor:pointer}
.sp-row:hover{background:rgba(128,128,128,.12)}
.sp-row.sel{background:rgba(224,168,0,.18)}
.sp-list{font-family:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace;font-size:13px}
.sp-none{opacity:.5;font-style:italic}
@media (max-width:900px){.sp-tab{width:34px;height:102px}.sp-name{font-size:8px}}
/* phone: a 13-stop group must fit next to the main and panel padding (16+16, 10+10) */
@media (max-width:520px){.sp-panel{padding:10px}.sp-tab{width:min(24px,calc((100vw - 88px)/13));height:72px}.sp-num{top:50%;font-size:13px}.sp-name{display:none}.sp-group{gap:3px}.sp-list{font-size:11px}}
</style>
</head>
<body>
<div class="top"><span class="t">MIDIControl</span><span class="u" id="upd">connecting…</span></div>
<nav id="tabs"></nav>
<main id="main"></main>
<script>
"use strict";
const TABS=[["overview","Overview"],["software","Software"],["base","Base"],["controller","Controller"],["diag","Diagnostics"],["stops","Stops"]];
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
function nodeTab(status,details,counters,events,extra){
  const s=el("section");
  s.append(el("h5",null,"Status"),blockStatus(status),el("h5",null,"Details"),blockTable(details));
  for(const [title,rows] of extra||[])s.append(el("h5",null,title),blockTable(rows));
  s.append(el("h5",null,"Counters"),blockTable(counters),el("h5",null,"Recent events"),blockEvents(events,false));
  return s;
}

// --- device details from INFO_RESP (GET_INFO every 15 s); null fields = not reported ---
function flag(v,on,off){return v==null?null:(v?on:off);}
function hasInfo(n){return f(n,"info_ms")!=null;}
// Uptime advanced by the time since the report, so it keeps counting between polls.
function uptimeNow(n){const u=f(n,"uptime_ms"),t=f(n,"info_ms");return u==null||t==null?null:u+(serverNow()-t);}
function infoNote(n,what){
  if(f(n,"info_unsupported"))return [{label:what,value:"not reported: firmware without GET_INFO (update it)",note:true}];
  return [{label:what,value:"not reported yet (asked every 15 s while reachable)",note:true}];
}
function firmwareRows(n){
  if(!hasInfo(n))return infoNote(n,"Firmware");
  const rev=f(n,"fw_revision"),br=f(n,"fw_branch"),up=uptimeNow(n),fc=f(n,"fault_count");
  return [
    {label:"Revision",value:rev?rev+(br?" ("+br+")":"")+(f(n,"fw_dirty")?" · uncommitted changes":""):null,mono:true},
    {label:"Built",value:f(n,"fw_built"),mono:true},
    {label:"Uptime",value:up!=null?dur(up)+" · booted "+stamp(serverNow()-up):null},
    {label:"Last reset cause",value:f(n,"reset_cause")},
    fc!=null?{label:"Faults since power-on",value:String(fc)}:null,
    {label:"Reported",value:ago(f(n,"info_ms"))}].filter(Boolean);
}
function bleChips(n,peerName,withStream){
  const parts=[];
  const add=(k,on,off)=>{const v=f(n,k);if(v!=null)parts.push((v?"✔ ":"✘ ")+(v?on:off));};
  add("ble_module_up","module ready","module not ready");
  add("ble_connected","connected","not connected");
  if(withStream)add("ble_stream_open","stream open","stream closed");  // the remote does not track it
  add("ble_secured","secured","not secured");
  add("ble_peer_alive",peerName+" alive",peerName+" silent");
  add("ble_pc_alive","PC alive","PC not seen");
  return parts.join(" · ");
}
function battText(n){
  const mv=f(n,"battery_mv"),lv=f(n,"battery_level");
  if(mv==null&&lv==null)return null;
  let s=(mv?(mv/1000).toFixed(2)+" V":"not measured yet")+(lv!=null?" · level "+lv+"/4":"");
  if(f(n,"battery_critical"))s+=" · CRITICAL";
  return s;
}
function chargeText(n){
  if(f(n,"vbus")==null)return null;
  if(!f(n,"vbus"))return "on battery (no USB)";
  const v=f(n,"vbus_mv");
  return "USB "+(v?(v/1000).toFixed(2)+" V":"present")+" · "+(f(n,"charging")?"charging":f(n,"charge_done")?"charged":"not charging");
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
  const op=f(n,"organ_power");
  s+=" · organ power "+(op==null?"unknown":op?"on":"off");
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
  const op=f(n,"organ_power");  // the SSR as the base reports it (STATUS_RESP), whoever switched it
  items.push(op==null?["grey","Organ power unknown"]:op?["green","Organ power on"]:["grey","Organ power off"]);
  if(hasInfo(n)&&f(n,"ble_secured")===false&&f(n,"ble_connected"))items.push(["yellow","BLE link not secured"]);
  if(f(n,"fw_dirty"))items.push(["yellow","Firmware with uncommitted changes"]);
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
    {label:"Church display",value:flag(f(n,"display"),"connected","not connected")}];
  const ble=hasInfo(n)?[
    {label:"State",value:bleChips(n,"remote",true)},
    {label:"Name",value:f(n,"ble_name"),mono:true},
    {label:"Paired remote",value:f(n,"ble_peer")||"none stored (pair from Nastaveni)",mono:true},
    {label:"Pairing",value:f(n,"ble_pairing")}]:infoNote(n,"Bluetooth");
  const counters=packetCounters(n);
  counters.push({label:"CRC errors / duplicates",value:c(n,"crc_errors")+" / "+c(n,"duplicates")});
  return nodeTab(items,details,counters,data.nodes[n].events.slice(),[["Firmware",firmwareRows(n)],["Bluetooth",ble]]);
}
// From the base's report, not from the dot (the dot also turns amber for an unseen fault or a low battery).
function baseReportFresh(){const sm=f("base","status_ms");return f("base","connected")&&sm!=null&&serverNow()-sm<=15000;}
function controllerStatus(){
  if(!f("base","connected"))return ["grey","Base not connected"];
  if(!baseReportFresh())return ["grey","No fresh report from the base"];
  return f("base","status_ble")?["green","BLE connected (per base)"]:["yellow","BLE disconnected (per base)"];
}
function controllerSummary(){
  const heard=ago(f("controller","last_rx_ms")),b=battText("controller");
  return controllerStatus()[1]+(b?" · battery "+b:"")+" · "+(heard?"last heard "+heard:"never heard");
}
function controller(){
  const n="controller",heard=ago(f(n,"last_rx_ms"));
  const items=[controllerStatus(),["grey",heard?"Last heard "+heard:"Never heard"]];
  const lv=f(n,"battery_level");
  if(f(n,"battery_critical"))items.push(["red","Battery critical"]);
  else if(lv!=null)items.push([lv<=1&&!f(n,"vbus")?"yellow":"green","Battery "+lv+"/4"+(f(n,"charging")?", charging":"")]);
  if(f(n,"fw_dirty"))items.push(["yellow","Firmware with uncommitted changes"]);
  const cmd=f(n,"last_cmd"),tx=f(n,"last_tx");
  const details=[
    {label:"Route",value:"via base, BLE"+(baseReportFresh()?" (base reports BLE "+yesno(f("base","status_ble"))+", "+ago(f("base","status_ms"))+")":"")},
    {label:"Last command",value:cmd?cmd+" at "+clock(f(n,"last_cmd_ms"))+" → "+(f(n,"last_cmd_reply")||"?"):null},
    {label:"Last sent to it",value:tx?tx+" at "+clock(f(n,"last_tx_ms"))+" · "+(f(n,"last_tx_result")||"?"):null}];
  const power=hasInfo(n)?[{label:"Battery",value:battText(n)},{label:"Supply",value:chargeText(n)}]:infoNote(n,"Battery");
  const ble=hasInfo(n)?[
    {label:"State",value:bleChips(n,"base",false)},
    {label:"Name",value:f(n,"ble_name"),mono:true},
    {label:"Connected base",value:f(n,"ble_peer"),mono:true}]:infoNote(n,"Bluetooth");
  const counters=packetCounters(n),cmds=[];
  for(const [k,v] of Object.entries(data.nodes[n].counters))if(k.startsWith("cmd."))cmds.push(k.slice(4)+" "+v);
  counters.push({label:"Commands",value:cmds.length?cmds.join(" · "):"none"});
  return nodeTab(items,details,counters,data.nodes[n].events.slice(),[["Power",power],["Firmware",firmwareRows(n)],["Bluetooth",ble]]);
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
const DG={sec:null,body:null,faults:null,resets:null,note:null,entries:[],resetEntries:[],resetsAt:0,node:"",level:"D",tag:"",text:"",timer:null};
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
    if(Date.now()-DG.resetsAt>10000){DG.resetEntries=await getDiag("kind=reset&limit=10");DG.resetsAt=Date.now();}
  }catch(e){}
  if(String(window.getSelection())==="")drawDiag();
}
function diagLoop(){
  DG.timer=null;
  if(current()!=="diag")return;  // polls only while the tab is open
  pollOnce().then(()=>{if(!DG.timer&&current()==="diag")DG.timer=setTimeout(diagLoop,1000);});
}
function diag(){diagSection();drawDiag();if(!DG.timer)diagLoop();return DG.sec;}

// --- Stops tab: built once and cached (the 1 s status poll must not rebuild it or lose edits).
// /api/stops is loaded when the tab is first opened and after each save, never polled. ---
const SP={sec:null,E:{},tabs:{},loaded:false,loadErr:false,layout:"",fitPending:false,groups:[],names:{},codes:[],saved:{},total:0,
  cur:null,draft:new Set(),dirty:false,edits:0,busy:false,sortK:"code",sortDir:1};
function spSorted(set){return [...set].sort((a,b)=>a-b);}
async function spFetch(url,opts){
  const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),5000);
  try{
    const r=await fetch(url,Object.assign({cache:"no-store",signal:ac.signal},opts));
    if(!r.ok)throw new Error((await r.text()).trim()||"HTTP "+r.status);
    return await r.json();
  }catch(e){throw e.name==="AbortError"?new Error("no answer from the player"):e;}
  finally{clearTimeout(timer);}
}
function spMsg(text,err){SP.E.msg.textContent=text;SP.E.msg.className="sp-msg"+(err?" err":"");}
async function spLoad(){
  const E=SP.E;
  try{
    const j=await spFetch("/api/stops");
    SP.groups=j.groups;SP.names=j.names;SP.codes=j.songs.map(s=>s.code);SP.saved={};
    for(const s of j.songs)SP.saved[s.code]=s.stops;
    SP.total=Math.max(0,...SP.groups.map(g=>g.last));
    SP.loaded=true;SP.loadErr=false;E.load.replaceChildren();
    const layout=JSON.stringify([j.groups,j.names]);
    if(layout!==SP.layout){SP.layout=layout;spBuildPanel();}  // keeps the tab buttons (and focus) across saves
    if(!SP.codes.includes(SP.cur)){SP.cur=SP.codes.length?SP.codes[0]:null;SP.dirty=false;}
    if(!SP.dirty)SP.draft=new Set(SP.cur!=null&&SP.saved[SP.cur]||[]);
  }catch(e){
    SP.loadErr=true;
    const b=el("button",null,"Try again");b.onclick=spLoad;
    E.load.replaceChildren(el("span","sp-err","Could not load the stops: "+e.message+" "),b);
  }
  spDraw();
}
function spBuildPanel(){
  const p=SP.E.panel;SP.tabs={};p.replaceChildren();
  for(const g of SP.groups){
    const col=el("div","sp-col"),row=el("div","sp-group");
    row.setAttribute("role","group");row.setAttribute("aria-label",g.label);
    for(let n=g.first;n<=g.last;n++){
      const name=SP.names[n]||"",t=el("button","sp-tab");
      t.type="button";t.title=name;t.setAttribute("aria-label",n+" "+name);
      t.append(el("span","sp-num",n),el("span","sp-name",name));
      t.onclick=()=>{if(SP.draft.has(n))SP.draft.delete(n);else SP.draft.add(n);spChanged();};
      SP.tabs[n]=t;row.append(t);
    }
    col.append(el("div","sp-glabel",g.label),row);p.append(col);
  }
  spFit();
}
// A name too wide for its tab (one long word) gets a smaller font; only as a last resort it breaks mid-word.
function spFit(){
  if(!SP.E.panel||!SP.E.panel.isConnected){SP.fitPending=true;return;}
  SP.fitPending=false;
  for(const n of SP.E.panel.querySelectorAll(".sp-name")){
    n.style.fontSize="";n.style.overflowWrap="";
    if(!n.clientWidth)continue;  // names hidden (phone)
    let fs=parseFloat(getComputedStyle(n).fontSize);
    while(n.scrollWidth>n.clientWidth&&fs>6){fs-=.5;n.style.fontSize=fs+"px";}
    if(n.scrollWidth>n.clientWidth)n.style.overflowWrap="anywhere";
  }
}
window.addEventListener("resize",()=>{if(SP.sec)requestAnimationFrame(spFit);});
function spChanged(){SP.dirty=true;SP.edits++;spMsg("");spDraw();}
function spDraw(){
  const E=SP.E,cur=SP.cur,saved=cur!=null?SP.saved[cur]:undefined,has=cur!=null;
  E.panel.hidden=E.actions.hidden=!has;
  E.cur.textContent=has?"Song "+cur:SP.loaded?"No songs in the play folder":SP.loadErr?"Stops not loaded":"Loading the stops…";
  E.count.textContent="";E.state.hidden=!has;
  if(has){
    if(SP.dirty){E.state.textContent="unsaved changes";E.state.className="sp-state dirty";}
    else if(saved===null)E.state.textContent="not set: organ stops are left as they are";
    else E.state.textContent=saved.length?"saved":"saved: all stops off";
    if(!SP.dirty)E.state.className="sp-state";
    if(SP.dirty||saved!==null)E.count.textContent=" · "+SP.draft.size+" of "+SP.total+" on";
  }
  E.panel.classList.toggle("notset",saved===null&&!SP.dirty);
  for(const [n,t] of Object.entries(SP.tabs))t.setAttribute("aria-pressed",SP.draft.has(Number(n))?"true":"false");
  E.save.disabled=!SP.dirty||SP.busy;
  E.alloff.disabled=SP.busy;
  E.notset.disabled=(saved===null&&!SP.dirty)||SP.busy;
  spDrawSongs();
}
function spListCmp(a,b){
  if(a===null||b===null)return (a!==null)-(b!==null);  // "not set" before everything
  for(let i=0;i<a.length&&i<b.length;i++)if(a[i]!==b[i])return a[i]-b[i];
  return a.length-b.length;
}
function spCmp(a,b){
  const byCode=a.code.localeCompare(b.code,undefined,{numeric:true});
  const d=SP.sortK==="code"?byCode:SP.sortK==="n"?a.n-b.n:spListCmp(a.v,b.v);
  return d*SP.sortDir||byCode;
}
function spDrawSongs(){
  const E=SP.E,a=document.activeElement,focus=E.tbody.contains(a)?a.dataset.code:null;
  E.tbody.replaceChildren();
  const rows=SP.codes.map(c=>{const v=SP.saved[c];return {code:c,v:v===null?null:spSorted(v),n:v===null?-1:v.length};});
  for(const r of rows.sort(spCmp)){
    const tr=el("tr","sp-row"+(r.code===SP.cur?" sel":"")),list=el("td","sp-list");
    tr.tabIndex=0;tr.dataset.code=r.code;
    if(r.code===SP.cur)tr.setAttribute("aria-current","true");
    list.append(r.v===null?el("span","sp-none","not set"):r.v.length?document.createTextNode(r.v.join(" ")):el("span","sp-none","all off"));
    tr.append(el("td",null,r.code),el("td",null,r.n<0?"–":r.n),list);
    tr.onclick=()=>spPick(r.code);
    tr.onkeydown=e=>{if(e.key==="Enter"||e.key===" "){e.preventDefault();spPick(r.code);}};
    E.tbody.append(tr);
    if(r.code===focus)tr.focus();  // keyboard selection keeps its place
  }
  for(const th of E.ths)th.setAttribute("aria-sort",th.dataset.k!==SP.sortK?"none":SP.sortDir>0?"ascending":"descending");
}
function spPick(code){
  if(code===SP.cur)return;
  if(SP.dirty&&!confirm("Discard unsaved changes to song "+SP.cur+"?"))return;
  SP.cur=code;SP.draft=new Set(SP.saved[code]||[]);SP.dirty=false;spMsg("");spDraw();
}
// PUT one song (stops = array, or null = "Not set"). A failure keeps the edits and shows the reason.
async function spPut(stops){
  const code=SP.cur,edits=SP.edits;
  SP.busy=true;spMsg("");spDraw();
  let ok=false;
  try{
    const j=await spFetch("/api/stops/"+encodeURIComponent(code),
      {method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify({stops})});
    SP.saved[code]=j.stops;
    if(SP.cur===code&&SP.edits===edits){SP.dirty=false;SP.draft=new Set(j.stops||[]);}
    ok=true;
  }catch(e){spMsg("Not saved: "+e.message,true);}
  SP.busy=false;spDraw();
  if(ok)spLoad();
  return ok;
}
function stops(){
  if(SP.sec){if(SP.fitPending)requestAnimationFrame(spFit);return SP.sec;}
  const s=el("section","sp"),E=SP.E;
  const head=el("div","sp-head");
  E.cur=el("span","sp-cur");E.count=el("span","sp-count");E.state=el("span","sp-state");
  const title=el("span");title.append(E.cur,E.count);  // "Song 401 · 16 of 42 on"
  head.append(title,E.state);
  E.load=el("div");E.panel=el("div","sp-panel");E.actions=el("div","sp-actions");
  E.save=el("button",null,"Save");E.alloff=el("button",null,"All off");E.notset=el("button","sp-small","Not set");
  E.msg=el("span","sp-msg");E.msg.setAttribute("role","status");
  E.save.onclick=async()=>{
    if(await spPut(spSorted(SP.draft))){E.save.textContent="Saved ✓";setTimeout(()=>{E.save.textContent="Save";},1200);}
  };
  E.alloff.onclick=()=>{if(SP.draft.size||SP.saved[SP.cur]===null){SP.draft.clear();spChanged();}};
  E.notset.onclick=()=>{
    if(confirm("Forget the stops of song "+SP.cur+"?"+(SP.dirty?" Your unsaved changes are discarded too.":"")+
               " The organ will then be left as it is when this song starts."))spPut(null);
  };
  E.actions.append(E.save,E.alloff,E.msg,E.notset);
  const t=el("table","sp-songs"),thead=el("thead"),hr=el("tr");E.ths=[];
  for(const [k,name] of [["code","Song"],["n","Stops on"],["list","Stops"]]){
    const th=el("th"),b=el("button",null,name);th.dataset.k=k;b.type="button";
    b.onclick=()=>{if(SP.sortK===k)SP.sortDir=-SP.sortDir;else{SP.sortK=k;SP.sortDir=1;}spDrawSongs();};
    th.append(b);hr.append(th);E.ths.push(th);
  }
  E.tbody=el("tbody");thead.append(hr);t.append(thead,E.tbody);
  s.append(head,E.load,E.panel,E.actions,t);
  SP.sec=s;spDraw();spLoad();
  return s;
}

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
  const builders={overview,software,base,controller,diag,stops};
  const sec=builders[current()]();sec.classList.add("on");
  const main=document.getElementById("main");
  if(main.firstChild!==sec)main.replaceChildren(sec);  // the cached Diagnostics and Stops sections keep focus and edits
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
