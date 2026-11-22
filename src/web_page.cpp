#include "midicontrol/web_page.hpp"

namespace mc::web {

namespace {

// Field and counter names are the ones CommHandler / main write into the
// StatusRegistry (see status_json.hpp for the JSON shape).
// Two views: the home page (Czech, for the organist: system health, now playing,
// the per-song stops editor) and Advanced (English, the full debug detail).
const char* const kPage = R"HTML(<!doctype html>
<html lang="cs">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>MIDIControl</title>
<style>
:root{color-scheme:light dark;
  --bg:#f2f3f5;--surface:#fff;--surface-2:#f6f7f9;--line:#e3e5e9;--line-2:#d0d4da;
  --fg:#1b1d21;--fg-2:#555b65;--fg-3:#646a74;
  --accent:#006fff;--accent-hover:#005ad1;--accent-text:#0058cc;--on-accent:#fff;--accent-soft:rgba(0,111,255,.09);
  --green:#1a9d52;--amber:#d48a00;--red:#dc3a40;--grey:#a4a9b1;
  --green-fg:#137a3f;--amber-fg:#8f5d00;--red-fg:#b52a30;
  --green-soft:rgba(26,157,82,.11);--amber-soft:rgba(212,138,0,.13);--red-soft:rgba(220,58,64,.10);
  --shadow:0 1px 2px rgba(20,26,36,.05),0 2px 6px rgba(20,26,36,.04);
  --mono:ui-monospace,SFMono-Regular,Menlo,Consolas,monospace}
@media (prefers-color-scheme: dark){:root{
  --bg:#111215;--surface:#1a1c20;--surface-2:#212429;--line:#2a2d33;--line-2:#3a3e46;
  --fg:#e8e9ec;--fg-2:#a9aeb6;--fg-3:#8c919a;
  --accent:#4a95ff;--accent-hover:#6aa8ff;--accent-text:#6aa8ff;--on-accent:#fff;--accent-soft:rgba(74,149,255,.15);
  --green:#34c06d;--amber:#efb02e;--red:#f0646a;--grey:#5f646d;
  --green-fg:#4fd384;--amber-fg:#f3c15a;--red-fg:#ff8a8f;
  --green-soft:rgba(52,192,109,.13);--amber-soft:rgba(239,176,46,.13);--red-soft:rgba(240,100,106,.13);
  --shadow:0 1px 2px rgba(0,0,0,.4)}}
*{box-sizing:border-box}
html{scrollbar-color:var(--line-2) transparent}
body{margin:0;background:var(--bg);color:var(--fg);font:14px/1.5 system-ui,-apple-system,"Segoe UI",Roboto,sans-serif;
  font-variant-numeric:tabular-nums;-webkit-font-smoothing:antialiased}
::selection{background:var(--accent-soft);color:inherit}
a{color:var(--accent-text);text-underline-offset:3px}
:focus-visible{outline:2px solid var(--accent);outline-offset:2px;border-radius:4px}
[hidden]{display:none!important}
.ic{width:20px;height:20px;flex:none;fill:none;stroke:currentColor;stroke-width:1.6;stroke-linecap:round;stroke-linejoin:round}

/* --- top bar --- */
.bar{position:sticky;top:0;z-index:5;display:flex;align-items:center;gap:20px;height:56px;padding:0 24px;
  background:var(--surface);border-bottom:1px solid var(--line)}
.brand{display:flex;align-items:center;gap:10px;color:var(--fg);text-decoration:none;font-weight:650;font-size:15px;letter-spacing:-.01em}
.mark{display:grid;place-items:center;width:30px;height:30px;border-radius:8px;background:var(--accent);color:var(--on-accent)}
.mark .ic{width:18px;height:18px;stroke-width:1.8}
.views{display:flex;gap:2px;padding:3px;border-radius:9px;background:var(--surface-2);border:1px solid var(--line)}
.views a{padding:5px 14px;border-radius:6px;color:var(--fg-2);text-decoration:none;font-weight:500;font-size:13px;transition:background .15s,color .15s}
.views a:hover{color:var(--fg)}
.views a[aria-current="page"]{background:var(--surface);color:var(--fg);box-shadow:var(--shadow)}
.conn{margin-left:auto;display:flex;align-items:center;gap:8px;color:var(--fg-3);font-size:12px;white-space:nowrap}
.conn.bad{color:var(--red-fg);font-weight:500}
.conn .short{display:none}

/* --- shared --- */
main{max-width:1440px;margin:0 auto;padding:24px}
.d{display:inline-block;flex:none;width:8px;height:8px;border-radius:50%;background:var(--grey)}
.green{--c:var(--green)}.yellow{--c:var(--amber)}.red{--c:var(--red)}.grey{--c:var(--grey)}
.d.green,.d.yellow,.d.red,.d.grey{background:var(--c)}
.panel{background:var(--surface);border:1px solid var(--line);border-radius:12px;box-shadow:var(--shadow);min-width:0}
.ph{display:flex;align-items:center;gap:12px;min-height:52px;padding:12px 20px;border-bottom:1px solid var(--line)}
.ph h2{margin:0;font-size:15px;font-weight:600;letter-spacing:-.005em}
.ph .aside{margin-left:auto;display:flex;align-items:center;gap:8px;color:var(--fg-2);font-size:13px}
.pb{padding:16px 20px}
.btn{appearance:none;display:inline-flex;align-items:center;gap:6px;height:36px;padding:0 16px;border-radius:8px;
  font:inherit;font-weight:500;cursor:pointer;border:1px solid var(--line-2);background:var(--surface);color:var(--fg);
  transition:background .15s,border-color .15s,color .15s}
.btn:hover{background:var(--surface-2)}
.btn.primary{background:var(--accent);border-color:var(--accent);color:var(--on-accent)}
.btn.primary:hover{background:var(--accent-hover);border-color:var(--accent-hover)}
.btn.quiet{border-color:transparent;background:none;color:var(--fg-2)}
.btn.quiet:hover{background:var(--surface-2);color:var(--fg)}
.btn.sm{height:28px;padding:0 10px;font-size:13px}
.btn:disabled{opacity:.45;cursor:default}
.btn:disabled:hover{background:var(--surface)}.btn.primary:disabled:hover{background:var(--accent)}.btn.quiet:disabled:hover{background:none}
.badge{display:inline-flex;align-items:center;gap:6px;height:24px;padding:0 10px;border-radius:12px;font-size:12px;font-weight:500;
  background:var(--surface-2);color:var(--fg-2);border:1px solid var(--line);white-space:nowrap}
.badge.ok{background:var(--green-soft);color:var(--green-fg);border-color:transparent}
.badge.warn{background:var(--amber-soft);color:var(--amber-fg);border-color:transparent}
.badge.bad{background:var(--red-soft);color:var(--red-fg);border-color:transparent}
.empty{color:var(--fg-3)}
.mono{font-family:var(--mono);font-size:12.5px}

/* --- home: notices --- */
.home{display:grid;gap:20px}
.notes{display:grid;gap:8px}
.note{display:flex;align-items:center;gap:12px;padding:11px 16px;border-radius:10px;background:var(--amber-soft);color:var(--fg)}
.note .ic{color:var(--amber-fg)}
.note.red{background:var(--red-soft)}.note.red .ic{color:var(--red-fg)}
.note a{margin-left:auto;display:inline-flex;align-items:center;gap:2px;font-weight:500;white-space:nowrap;text-decoration:none}
.note a .ic{width:16px;height:16px;color:inherit}
.note a:hover{text-decoration:underline}

/* --- home: system topology (signature: the links carry live traffic) --- */
.topo{display:flex;align-items:flex-start;padding:28px 20px 24px}
.node{flex:0 0 230px;display:flex;flex-direction:column;align-items:center;text-align:center;gap:2px;min-width:0;padding:8px;border-radius:12px;color:inherit;text-decoration:none;transition:background .15s}
.node:hover{background:var(--surface-2)}.node:hover .fact{background:var(--surface)}
.badge-ic{position:relative;display:grid;place-items:center;width:60px;height:60px;margin-bottom:10px;border-radius:50%;
  background:var(--surface-2);color:var(--fg);box-shadow:0 0 0 2px var(--c,var(--grey)),0 0 0 6px var(--surface)}
.badge-ic .ic{width:26px;height:26px;stroke-width:1.5}
.badge-ic .d{position:absolute;right:2px;bottom:2px;width:12px;height:12px;box-shadow:0 0 0 2px var(--surface)}
.node .nm{font-weight:600;font-size:15px}
.node .st{font-size:13px;color:var(--fg-2)}
.node .st.green{color:var(--green-fg)}.node .st.yellow{color:var(--amber-fg)}.node .st.red{color:var(--red-fg)}
.node .fact{white-space:nowrap;display:flex;align-items:center;gap:6px;margin-top:8px;padding:4px 10px;border-radius:8px;background:var(--surface-2);font-size:12.5px;color:var(--fg-2)}
.node .fact .ic{width:15px;height:15px}
.link{flex:1 1 auto;min-width:40px;margin-top:22px;display:flex;flex-direction:column;align-items:center;gap:6px}
.link .lb{font-size:11px;font-weight:600;letter-spacing:.04em;color:var(--fg-3)}
.wire{width:100%;height:2px;border-radius:1px;background:var(--line-2)}
.wire.off{background:repeating-linear-gradient(90deg,var(--line-2) 0 6px,transparent 6px 11px)}
.wire.on{background:linear-gradient(90deg,var(--green) 0 40%,color-mix(in srgb,var(--green) 35%,transparent) 50%,var(--green) 60% 100%) 0 0/220% 100%;
  animation:flow 2.4s linear infinite}
.wire.bad{background:var(--red)}
@keyframes flow{from{background-position:110% 0}to{background-position:-110% 0}}
.batt{display:inline-block;width:24px;height:12px;padding:2px;border:1px solid var(--fg-3);border-radius:3px;box-sizing:border-box}
.batt i{display:block;height:100%;border-radius:1px;background:var(--fg-2)}
.batt.low{border-color:var(--amber)}.batt.low i{background:var(--amber)}
.batt.crit{border-color:var(--red)}.batt.crit i{background:var(--red)}
.now{display:flex;align-items:center;gap:14px;flex-wrap:wrap;padding:14px 20px;border-top:1px solid var(--line);background:var(--surface-2);border-radius:0 0 12px 12px}
.now .k{display:flex;align-items:center;gap:8px;color:var(--fg-2);font-size:13px}
.now .v{font-weight:600;font-size:15px}
.now .sub{color:var(--fg-2);font-size:13px}
.pulse{position:relative;width:10px;height:10px;border-radius:50%;background:var(--green)}
.pulse::after{content:"";position:absolute;inset:0;border-radius:50%;background:var(--green);animation:ping 1.6s cubic-bezier(.2,.7,.3,1) infinite}
@keyframes ping{from{transform:scale(1);opacity:.6}to{transform:scale(2.6);opacity:0}}
.panel.stale .topo,.panel.stale .now{opacity:.45;filter:grayscale(1)}
.skel{height:180px;display:grid;place-items:center;color:var(--fg-3)}

/* --- home: stops editor --- */
#stops{scroll-margin-top:72px}
.sp-body{display:grid;grid-template-columns:248px minmax(0,1fr)}
/* the song list follows the editor while it scrolls; the list itself scrolls (100+ songs) */
.sp-side{border-right:1px solid var(--line);display:flex;flex-direction:column;min-height:0;position:sticky;top:56px;align-self:start;max-height:calc(100vh - 56px)}
.sp-tools{display:grid;gap:8px;padding:12px;border-bottom:1px solid var(--line)}
.sp-search{position:relative}
.sp-search .ic{position:absolute;left:9px;top:8px;width:18px;height:18px;color:var(--fg-3);pointer-events:none}
.sp-search input{width:100%;height:34px;padding:0 10px 0 34px;font:inherit;color:var(--fg);background:var(--surface);border:1px solid var(--line-2);border-radius:8px}
.sp-search input::placeholder{color:var(--fg-3)}
.sp-tools select{width:100%;height:30px;padding:0 8px;font:inherit;font-size:12.5px;color:var(--fg);background:var(--surface);border:1px solid var(--line-2);border-radius:7px}
.seg.full{width:100%}.seg.full button{flex:1}
.seg{display:flex;padding:2px;border-radius:7px;background:var(--surface-2);border:1px solid var(--line)}
.seg button{appearance:none;border:0;background:none;font:inherit;font-size:12px;color:var(--fg-2);padding:3px 10px;border-radius:5px;cursor:pointer}
.seg button[aria-pressed="true"]{background:var(--surface);color:var(--fg);box-shadow:var(--shadow);font-weight:500}
.sp-songs{position:relative;list-style:none;margin:0;padding:6px;overflow:auto;flex:1 1 auto;min-height:160px;overscroll-behavior:contain}
.sp-empty{padding:16px 10px;color:var(--fg-3);font-size:13px}
.sp-foot{padding:8px 14px;border-top:1px solid var(--line);font-size:12px;color:var(--fg-3)}
.sp-row{display:grid;grid-template-columns:1fr auto;gap:0 10px;align-items:baseline;padding:8px 10px;border-radius:8px;cursor:pointer;outline-offset:-2px}
.sp-row:hover{background:var(--surface-2)}
.sp-row.sel{background:var(--accent-soft)}
.sp-row .code{font-weight:600;font-size:14px}
.sp-row.sel .code{color:var(--accent-text)}
.sp-row .cnt{font-size:12px;color:var(--fg-2)}
.sp-row .list{grid-column:1/-1;font-family:var(--mono);font-size:11.5px;color:var(--fg-3);white-space:nowrap;overflow:hidden;text-overflow:ellipsis}
.sp-none{color:var(--fg-3)}
.sp-main{padding:20px;min-width:0}
.sp-head{display:flex;align-items:center;gap:12px;flex-wrap:wrap;margin-bottom:16px}
.sp-cur{scroll-margin-top:72px;margin:0;font-size:20px;font-weight:650;letter-spacing:-.015em}
.sp-count{color:var(--fg-2);font-size:13px}
.sp-load{margin-bottom:12px}
.sp-err{color:var(--red-fg)}
.sp-panel{display:flex;flex-wrap:wrap;gap:20px 32px;padding:20px;border-radius:10px;background:var(--surface-2);border:1px solid var(--line)}
.sp-col{display:flex;flex-direction:column;gap:10px}
.sp-glabel{font-size:11px;font-weight:600;letter-spacing:.06em;text-transform:uppercase;color:var(--fg-2);display:flex;align-items:center;gap:8px}
.sp-glabel::after{content:"";flex:1;height:1px;background:var(--line-2)}
.sp-group{display:flex;flex-wrap:wrap;gap:4px}
.sp-tab{appearance:none;margin:0;padding:0;font:inherit;width:62px;height:104px;border-radius:7px;position:relative;cursor:pointer;user-select:none;
  background:var(--surface);color:var(--fg-2);border:1px solid var(--line-2);transition:background .15s,color .15s,border-color .15s,transform .1s}
.sp-tab:hover{border-color:var(--fg-3);color:var(--fg)}
.sp-tab:active{transform:translateY(1px)}
.sp-tab[aria-pressed="true"]{background:var(--accent);border-color:var(--accent);color:var(--on-accent);box-shadow:0 2px 6px color-mix(in srgb,var(--accent) 35%,transparent)}
.sp-num{position:absolute;top:12px;left:0;right:0;text-align:center;font-weight:650;font-size:15px}
.sp-name{position:absolute;top:38px;left:2px;right:2px;text-align:center;font-size:11px;line-height:1.25;letter-spacing:-.015em;font-weight:500;opacity:.9;
  overflow-wrap:break-word;hyphens:auto}  /* wraps at spaces; only a word longer than the tab breaks */
.sp-panel.notset .sp-tab{opacity:.5;border-style:dashed}
.sp-actions{display:flex;flex-wrap:wrap;gap:10px;align-items:center;margin-top:8px;padding:12px 0;background:var(--surface)}
.sp-actions.dirty{position:sticky;bottom:0;z-index:2;box-shadow:0 -1px 0 var(--line)}  /* unsaved: Save stays in reach below a tall panel */
.sp-msg{flex:1 1 12em;font-size:13px;color:var(--fg-2)}
.sp-msg:empty{flex-basis:0}
.sp-msg.err{color:var(--red-fg)}
.sp-hint{margin:0 0 16px;color:var(--fg-2);font-size:13px;max-width:70ch}

/* --- advanced --- */
.adv{display:grid;grid-template-columns:220px minmax(0,1fr);gap:24px;align-items:start}
.subnav{position:sticky;top:80px;display:flex;flex-direction:column;gap:2px}
.subnav a{display:flex;align-items:center;gap:10px;padding:8px 12px;border-radius:8px;color:var(--fg-2);text-decoration:none;font-weight:500}
.subnav a:hover{background:var(--surface);color:var(--fg)}
.subnav a[aria-current="page"]{background:var(--surface);color:var(--fg);box-shadow:var(--shadow)}
.subnav a .d{margin-left:auto}
.adv-body{display:grid;gap:20px;min-width:0}
.adv-title{margin:0;font-size:22px;font-weight:650;letter-spacing:-.02em}
.chips{display:flex;flex-wrap:wrap;gap:8px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(380px,1fr));gap:20px;align-items:start}
table{border-collapse:collapse;width:100%}
.kv td{padding:9px 20px;border-top:1px solid var(--line);vertical-align:top}
.kv tr:first-child td{border-top:0}
.kv td:first-child{color:var(--fg-2);width:180px;white-space:nowrap}
.kv td.note{color:var(--fg-3);font-style:italic}
.ev td{padding:6px 12px;border-top:1px solid var(--line);font-family:var(--mono);font-size:12px;vertical-align:top}
.ev tr:first-child td{border-top:0}
.ev td:first-child{padding-left:20px;color:var(--fg-3);white-space:nowrap;width:1%}
.ev td:last-child{padding-right:20px;overflow-wrap:anywhere}
.ev td.k{white-space:nowrap;width:1%;color:var(--fg-2)}
.ev .E td,.ev .ERROR td{color:var(--red-fg)}.ev .W td,.ev .WARN td{color:var(--amber-fg)}.ev .D td.k{color:var(--fg-3)}
.scroll{max-height:460px;overflow:auto}
.pb.empty{padding:16px 20px}
.ov td{padding:12px 20px;border-top:1px solid var(--line);vertical-align:top}
.ov tr:first-child td{border-top:0}
.ov td:first-child{width:170px;font-weight:600;white-space:nowrap}
.ov td:first-child .d{margin-right:10px}
.ov a{color:inherit;text-decoration:none}.ov a:hover{color:var(--accent)}
.songs{display:flex;flex-wrap:wrap;gap:4px 10px}
.warn{color:var(--amber-fg)}
.dg-bar{display:flex;flex-wrap:wrap;gap:8px;padding:12px 20px;border-bottom:1px solid var(--line)}
.dg-bar select,.dg-bar input{height:32px;font:inherit;font-size:13px;color:var(--fg);background:var(--surface);border:1px solid var(--line-2);border-radius:7px;padding:0 10px}
.dg-bar input{width:12em}.dg-bar input.wide{width:18em;flex:0 1 auto}
.unseen{color:var(--amber-fg);font-weight:600}
.fw-drop{position:relative;display:flex;flex-direction:column;align-items:center;gap:6px;padding:22px 16px;text-align:center;cursor:pointer;
  border:1.5px dashed var(--line-2);border-radius:10px;color:var(--fg-2);transition:background .15s,border-color .15s}
.fw-drop:hover,.fw-drop.over{border-color:var(--accent);background:var(--accent-soft);color:var(--fg)}
.fw-drop:focus-within{outline:2px solid var(--accent);outline-offset:2px}
.fw-drop .ic{width:26px;height:26px;color:var(--accent-text)}
.fw-drop input{position:absolute;width:1px;height:1px;opacity:0;pointer-events:none}
.fw-drop.off{cursor:default;opacity:.55}.fw-drop.off:hover{border-color:var(--line-2);background:none;color:var(--fg-2)}
.fw-file{margin-top:14px}
.fw-err{display:flex;gap:10px;align-items:flex-start;margin-top:12px;padding:10px 14px;border-radius:8px;background:var(--red-soft);color:var(--fg)}
.fw-err .ic{color:var(--red-fg)}
.fw-act{display:flex;flex-wrap:wrap;align-items:center;gap:10px;margin-top:14px}
.fw-why{font-size:13px;color:var(--fg-2)}
.fw-run{margin-top:14px;display:grid;gap:6px}
.fw-bar{height:8px;border-radius:4px;background:var(--surface-2);border:1px solid var(--line);overflow:hidden}
.fw-bar i{display:block;height:100%;background:var(--accent);transition:width .4s}
.fw-msg{font-size:13px;color:var(--fg-2);overflow-wrap:anywhere}

@media (max-width:1100px){.sp-body{grid-template-columns:1fr}.sp-side{position:static;max-height:none;border-right:0;border-bottom:1px solid var(--line)}.sp-songs{flex:none;max-height:260px}}
@media (max-width:860px){
  .adv{grid-template-columns:1fr;gap:16px}
  .subnav{position:static;flex-direction:row;overflow-x:auto;padding-bottom:2px;mask-image:linear-gradient(90deg,#000 85%,transparent)}
  .subnav a{white-space:nowrap}.subnav a .d{margin-left:0}
  .grid{grid-template-columns:1fr}
  .topo{flex-direction:column;align-items:stretch;padding:12px 16px}
  .node{flex:none;display:grid;grid-template-columns:auto 1fr;grid-template-rows:auto auto auto;column-gap:14px;text-align:left;align-items:center}
  .badge-ic{grid-row:1/4;width:48px;height:48px;margin:0}.badge-ic .ic{width:22px;height:22px}
  .node .fact{justify-self:start;margin-top:4px}
  .link{flex-direction:row;margin:0 0 0 23px;height:30px;min-width:0}
  .link .wire{order:-1;width:2px;height:100%}
  .wire.on{background:linear-gradient(180deg,var(--green) 0 40%,color-mix(in srgb,var(--green) 35%,transparent) 50%,var(--green) 60% 100%) 0 0/100% 220%;animation-name:flowv}
  .wire.off{background:repeating-linear-gradient(180deg,var(--line-2) 0 5px,transparent 5px 9px)}
  @keyframes flowv{from{background-position:0 110%}to{background-position:0 -110%}}
}
@media (max-width:600px){
  main{padding:16px}
  .bar{padding:0 16px;gap:12px}.brand>span:last-child{display:none}.conn .tx{display:none}.conn .short{display:inline}
  .kv td:first-child{width:120px}
  .ph{padding:12px 16px}.kv td{padding:9px 16px}.kv td:first-child{white-space:normal}
  .sp-main{padding:16px 12px}
  /* phone: groups wrap into rows of tabs at least 56 px wide that keep their names (tap target, organist reads names) */
  .sp-panel{padding:12px;gap:16px}.sp-col{width:100%}
  .sp-group{display:grid;grid-template-columns:repeat(auto-fill,minmax(56px,1fr));gap:4px}
  .sp-tab{width:auto;height:80px}.sp-num{top:8px;font-size:14px}.sp-name{top:30px;font-size:11px}
  .grid{gap:16px}.ev td:first-child{padding-left:16px}
}
@media (prefers-reduced-motion: reduce){.wire.on,.pulse::after{animation:none}}
</style>
</head>
<body>
<svg width="0" height="0" style="position:absolute" aria-hidden="true">
<symbol id="i-organ" viewBox="0 0 24 24"><rect x="3.5" y="9" width="3.4" height="11" rx="1.2"/><rect x="10.3" y="4" width="3.4" height="16" rx="1.2"/><rect x="17.1" y="7" width="3.4" height="13" rx="1.2"/></symbol>
<symbol id="i-pc" viewBox="0 0 24 24"><rect x="3" y="4" width="18" height="12.5" rx="2"/><path d="M9 20.5h6M12 16.5v4"/></symbol>
<symbol id="i-base" viewBox="0 0 24 24"><rect x="3" y="9" width="18" height="10" rx="2"/><path d="M17 9V4.5"/><circle cx="7.5" cy="14" r="1.2"/><path d="M11 14h6"/></symbol>
<symbol id="i-remote" viewBox="0 0 24 24"><rect x="7" y="2.5" width="10" height="19" rx="3"/><circle cx="12" cy="8" r="2"/><path d="M10 13.5h4M10 16.5h4"/></symbol>
<symbol id="i-power" viewBox="0 0 24 24"><path d="M12 3.5v8"/><path d="M6.6 7.2a7.5 7.5 0 1 0 10.8 0"/></symbol>
<symbol id="i-note" viewBox="0 0 24 24"><path d="M9 18V5.5l10-2V16"/><circle cx="6.5" cy="18" r="2.5"/><circle cx="16.5" cy="16" r="2.5"/></symbol>
<symbol id="i-alert" viewBox="0 0 24 24"><path d="M10.3 4.3 2.6 18a2 2 0 0 0 1.7 3h15.4a2 2 0 0 0 1.7-3L13.7 4.3a2 2 0 0 0-3.4 0z"/><path d="M12 10v4M12 17.2v.1"/></symbol>
<symbol id="i-check" viewBox="0 0 24 24"><circle cx="12" cy="12" r="9"/><path d="m8 12.5 2.8 2.8L16.2 9.5"/></symbol>
<symbol id="i-search" viewBox="0 0 24 24"><circle cx="11" cy="11" r="6.5"/><path d="m16 16 4.5 4.5"/></symbol>
<symbol id="i-upload" viewBox="0 0 24 24"><path d="M12 15.5V4M7.5 8.5 12 4l4.5 4.5"/><path d="M4 14.5v3a2.5 2.5 0 0 0 2.5 2.5h11a2.5 2.5 0 0 0 2.5-2.5v-3"/></symbol>
<symbol id="i-chev" viewBox="0 0 24 24"><path d="m9 6 6 6-6 6"/></symbol>
</svg>
<template id="ic"><svg class="ic" aria-hidden="true"><use href=""></use></svg></template>
<header class="bar">
  <a class="brand" href="#"><span class="mark" id="mark"></span><span>MIDIControl</span></a>
  <nav class="views" id="views" aria-label="Zobrazení"></nav>
  <span class="conn" id="upd">připojování…</span>
</header>
<main id="main"></main>
<script>
"use strict";
const ADV=[["overview","Overview"],["software","Software"],["base","Base"],["controller","Controller"],["diag","Diagnostics"],["firmware","Firmware"]];
let data=null, fetchedAt=0, failed=false;

function el(tag,cls,text){const e=document.createElement(tag);if(cls)e.className=cls;if(text!=null)e.textContent=String(text);return e;}
function icon(name){const s=document.getElementById("ic").content.firstElementChild.cloneNode(true);s.firstElementChild.setAttribute("href","#i-"+name);return s;}
function dot(color){return el("span","d "+color);}
function pad(n,w){return String(n).padStart(w||2,"0");}
// Czech plural: 1 / 2-4 / 0, 5+
function cz(n,one,few,many){return n+" "+(n===1?one:n>=2&&n<=4?few:many);}
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
function yesno(v){return v==null?"?":(v?"yes":"no");}
function panel(title,body,aside){
  const p=el("div","panel"),h=el("div","ph");h.append(el("h2",null,title));
  if(aside){const a=el("div","aside");a.append(aside);h.append(a);}
  p.append(h);if(body)p.append(body);return p;
}

// ================= Advanced (English) =================
// --- the four blocks, same on every node page ---
function blockStatus(items){
  const d=el("div","chips");
  for(const [color,text] of items){
    const b=el("span","badge "+({green:"ok",yellow:"warn",red:"bad"}[color]||""));b.append(dot(color),document.createTextNode(text));d.append(b);
  }
  return d;
}
function blockTable(rows){
  const t=el("table","kv");
  for(const r of rows){
    const tr=el("tr"),k=el("td",null,r.label),v=el("td",r.note?"note":(r.mono?"mono":null));
    if(r.node)v.append(r.node);else v.textContent=r.value==null||r.value===""?"—":r.value;
    tr.append(k,v);t.append(tr);
  }
  return t;
}
function blockEvents(events,withNode){
  if(!events.length)return el("div","pb empty","No events yet.");
  const t=el("table","ev");
  for(const e of events){
    const tr=el("tr",e.level);let kind=e.level,text=e.text;
    const m=/^(TX|RX) (.*)$/.exec(text);
    if(e.level==="DEBUG"&&m){kind=m[1];text=m[2];}
    tr.append(el("td",null,clock(e.t,!withNode)),el("td","k",kind));
    if(withNode)tr.append(el("td","k",ADV.find(x=>x[0]===e.node)[1]));
    tr.append(el("td",null,text));t.append(tr);
  }
  const w=el("div","scroll");w.append(t);return w;
}
function nodePage(status,details,counters,events,extra){
  const s=el("div","adv-body"),g=el("div","grid");
  g.append(panel("Details",blockTable(details)));
  for(const [title,rows] of extra||[])g.append(panel(title,blockTable(rows)));
  g.append(panel("Counters",blockTable(counters)));
  s.append(blockStatus(status),g,panel("Recent events",blockEvents(events,false)));
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
  const add=(k,on,off)=>{const v=f(n,k);if(v!=null)parts.push(v?on:off);};
  add("ble_module_up","module ready","module NOT ready");
  add("ble_connected","connected","NOT connected");
  if(withStream)add("ble_stream_open","stream open","stream closed");  // the remote does not track it
  add("ble_secured","secured","NOT secured");
  add("ble_peer_alive",peerName+" alive",peerName+" silent");
  add("ble_pc_alive","PC alive","PC not seen");
  return parts.join(" · ");
}
// State of charge in %. A remote firmware older than BATTERY_PERCENT sends only the voltage (and a 0..4 level):
// the same Li-ion curve as the remote's logic/battery_soc.cpp, without its filtering.
const BATT_CURVE=[[4180,100],[4100,92],[4000,82],[3920,72],[3870,64],[3820,55],[3790,47],[3760,39],[3730,31],[3700,23],[3670,16],[3630,10],[3560,5],[3450,2],[3300,0]];
function pctFromMv(mv){
  if(mv>=BATT_CURVE[0][0])return 100;
  for(let i=1;i<BATT_CURVE.length;i++){const [hm,hp]=BATT_CURVE[i-1],[lm,lp]=BATT_CURVE[i];if(mv>=lm)return Math.round(lp+(mv-lm)*(hp-lp)/(hm-lm));}
  return 0;
}
function battPct(n){
  const p=f(n,"battery_percent");if(p!=null)return p;
  const mv=f(n,"battery_mv");if(mv)return pctFromMv(mv);
  const lv=f(n,"battery_level");return lv!=null?lv*25:null;
}
function battLow(n){const p=battPct(n);return p!=null&&p<=15&&!f(n,"vbus");}
function battText(n){
  const mv=f(n,"battery_mv"),p=battPct(n);
  if(mv==null&&p==null)return null;
  let s=(mv?(mv/1000).toFixed(2)+" V":"not measured yet")+(p!=null?" · "+p+" %":"");
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
  const songs=el("span","songs mono");
  for(const s of data.songs){
    const x=el("span",s.problem?"warn":null,s.code+(s.problem?" (!)":""));
    if(s.problem)x.title=s.problem;
    songs.append(x);
  }
  const st=f(n,"started_ms"),ver=f(n,"version");
  const details=[
    {label:"Version",value:ver?ver+" (built "+(f(n,"build_date")||"?")+")":null,mono:true},
    {label:"Started",value:st!=null?stamp(st)+" · uptime "+dur(serverNow()-st):null},
    {label:"Config",value:f(n,"config_path"),mono:true},
    {label:"Play folder",value:f(n,"play_folder")!=null?f(n,"play_folder")+" · "+(f(n,"song_count")||0)+" songs":null,mono:true},
    {label:"Playback",value:playbackText()},
    {label:"Stops",value:f(n,"stops")||null},
    data.songs.length?{label:"Songs",node:songs}:{label:"Songs",value:null}];
  for(const s of data.songs)if(s.problem)details.push({label:"Song "+s.code,value:s.problem});
  const ended=c(n,"songs_ended"),stopped=c(n,"songs_stopped");
  const counters=[{label:"Songs played",value:(ended+stopped)+" ("+ended+" ended, "+stopped+" stopped)"},
                  {label:"Warnings / errors",value:c(n,"warnings")+" / "+c(n,"errors")}];
  return nodePage(items,details,counters,data.nodes[n].events.slice());
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
  if(f(n,"organ_idle_off_armed"))items.push(["grey","Idle-off armed"]);
  if(f(n,"organ_power_fault"))items.push(["red","Organ power fault"]);
  if(hasInfo(n)&&f(n,"ble_secured")===false&&f(n,"ble_connected"))items.push(["yellow","BLE link not secured"]);
  if(f(n,"fw_dirty"))items.push(["yellow","Firmware with uncommitted changes"]);
  if(conn&&f(n,"status_answered")===false)items.push(["yellow","GET_STATUS not answered"]);
  if(data.nodes[n].unseen_faults>0)items.push(["yellow","Unseen fault (Diagnostics)"]);
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
    {label:"Organ power fault",value:f(n,"organ_power_fault")||"none"},
    {label:"Idle power-off",value:f(n,"organ_idle_off_armed")?"armed (organ switches off after the idle time)":"not armed"},
    {label:"Connected since",value:(conn&&since!=null?stamp(since):"not connected")+" (reconnects: "+(f(n,"reconnects")||0)+")"},
    {label:"Base reports",value:reports},
    {label:"Church display",value:flag(f(n,"display"),"connected","not connected")}];
  const ble=hasInfo(n)?[
    {label:"State",value:bleChips(n,"remote",true)},
    {label:"Name",value:f(n,"ble_name"),mono:true},
    {label:"Paired remote",value:f(n,"ble_peer")||"none stored (pair from Nastaveni)",mono:true},
    {label:"Pairing",value:f(n,"ble_pairing")},
    {label:"Signal (this unit)",value:f(n,"link_rssi")!=null?f(n,"link_rssi")+" dBm":null},
    {label:"Signal (peer measured)",value:f(n,"link_peer_rssi")!=null?f(n,"link_peer_rssi")+" dBm":null},
    {label:"Link parameters",value:f(n,"link_interval_ms")!=null?"interval "+f(n,"link_interval_ms")+" ms · latency "+f(n,"link_latency")+" · timeout "+f(n,"link_timeout_ms")+" ms · PHY "+(f(n,"link_phy")||"?"):null},
    {label:"Link losses",value:f(n,"link_losses")!=null?f(n,"link_losses")+" (ERR_CONN "+f(n,"link_err_conn")+") · module reboots "+f(n,"link_module_reboots")+" · secure retries "+f(n,"link_secure_retries")+" · last reconnect "+f(n,"link_reconnect_ms")+" ms":null}]:infoNote(n,"Bluetooth");
  const counters=packetCounters(n);
  counters.push({label:"CRC errors / duplicates",value:c(n,"crc_errors")+" / "+c(n,"duplicates")});
  return nodePage(items,details,counters,data.nodes[n].events.slice(),[["Firmware",firmwareRows(n)],["Bluetooth",ble]]);
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
  const p=battPct(n);
  if(f(n,"battery_critical"))items.push(["red","Battery critical"]);
  else if(p!=null)items.push([battLow(n)?"yellow":"green","Battery "+p+" %"+(f(n,"charging")?", charging":"")]);
  if(f(n,"fw_dirty"))items.push(["yellow","Firmware with uncommitted changes"]);
  if(data.nodes[n].unseen_faults>0)items.push(["yellow","Unseen fault (Diagnostics)"]);
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
  return nodePage(items,details,counters,data.nodes[n].events.slice(),[["Power",power],["Firmware",firmwareRows(n)],["Bluetooth",ble]]);
}
function overview(){
  const s=el("div","adv-body"),t=el("table","ov");
  const rows=[["software","Software",softwareSummary()],["base","Base",baseSummary()],["controller","Controller",controllerSummary()]];
  for(const [k,name,text] of rows){
    const tr=el("tr"),a=el("td"),l=el("a",null);l.href="#adv/"+k;
    l.append(dot(data.nodes[k].dot),document.createTextNode(name));a.append(l);
    tr.append(a,el("td",null,text));t.append(tr);
  }
  s.append(panel("Summary",t),panel("Problems",blockEvents(data.problems,true)));
  return s;
}

// --- Diagnostics page: built once (the filter inputs must keep focus), filled by its own 1 s poll ---
const DG={sec:null,body:null,faults:null,resets:null,entries:[],resetEntries:[],resetsAt:0,node:"",level:"D",tag:"",text:"",timer:null};
function hex(v){return "0x"+(v>>>0).toString(16).padStart(8,"0");}
function faultTable(name,node,fl,unseen){
  let aside=null;
  if(fl&&unseen>0){
    aside=el("span");
    const b=el("button","btn sm","Mark seen");
    b.onclick=async()=>{b.disabled=true;try{await fetch("/api/diag/ack",{method:"POST",headers:{"Content-Type":"application/x-www-form-urlencoded"},body:"id="+fl.id,cache:"no-store"});}catch(e){}pollOnce();};
    aside.append(el("span","badge warn","unseen"),b);aside.style.display="contents";
  }
  if(!fl)return panel(name+": last fault",el("div","pb empty","no fault recorded"));
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
  const t=el("table","kv mono");
  for(const [k,v] of rows){const tr=el("tr");tr.append(el("td",null,k),el("td",unseen>0&&k==="When"?"unseen":null,v));t.append(tr);}
  return panel(name+": last fault",t,aside);
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
  const rt=el("table","ev");let n=0;
  for(const e of DG.resetEntries){
    const r=resetText(e);if(r===null)continue;
    const tr=el("tr");tr.append(el("td",null,stamp(e.t)),el("td","k",e.node),el("td",null,r));rt.append(tr);
    if(++n>=10)break;
  }
  DG.resets.replaceChildren(n?rt:el("div","pb empty","none recorded"));
  const q=DG.text.toLowerCase(),tg=DG.tag.toLowerCase(),t=el("table","ev");
  let shown=0;
  for(const e of DG.entries){
    if(tg&&!e.tag.toLowerCase().includes(tg))continue;
    if(q&&!e.text.toLowerCase().includes(q))continue;
    const tr=el("tr",e.lvl);
    tr.append(el("td",null,clock(e.t,true)),el("td","k",e.node),el("td","k",e.lvl),el("td","k",e.tag),el("td",null,e.text));
    t.append(tr);shown++;
  }
  DG.body.replaceChildren(shown?t:el("div","pb empty","no entries"));
}
function diagSection(){
  if(DG.sec)return DG.sec;
  const s=el("div","adv-body");
  DG.faults=el("div","grid");DG.resets=el("div");DG.body=el("div","scroll");
  const bar=el("div","dg-bar");
  const node=el("select"),level=el("select"),tag=el("input"),text=el("input","wide");
  for(const [v,n] of [["","both nodes"],["base","base"],["remote","remote"]]){const o=el("option",null,n);o.value=v;node.append(o);}
  for(const [v,n] of [["D","all levels"],["I","info+"],["W","warnings+"],["E","errors"]]){const o=el("option",null,n);o.value=v;level.append(o);}
  tag.placeholder="tag";text.placeholder="search text";
  node.setAttribute("aria-label","node");level.setAttribute("aria-label","level");tag.setAttribute("aria-label","tag");text.setAttribute("aria-label","search text");
  node.onchange=()=>{DG.node=node.value;pollOnce();};
  level.onchange=()=>{DG.level=level.value;pollOnce();};
  tag.oninput=()=>{DG.tag=tag.value;drawDiag();};
  text.oninput=()=>{DG.text=text.value;drawDiag();};
  bar.append(node,level,tag,text);
  const log=panel("Log",null);log.append(bar,DG.body);
  s.append(DG.faults,panel("Reset causes",DG.resets),log);
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
  if(route().tab!=="diag")return;  // polls only while the page is open
  pollOnce().then(()=>{if(!DG.timer&&route().tab==="diag")DG.timer=setTimeout(diagLoop,1000);});
}
function diag(){diagSection();drawDiag();if(!DG.timer)diagLoop();return DG.sec;}

// --- Firmware page: one card per unit; built once (the chosen file must survive the 1 s redraw) ---
// The file is the <name>-update.bin of the firmware build. Its trailer is read here before anything is
// sent, so the other unit's image is refused at once; the service (fwimage::check) and the device
// (FW_UPDATE_START target, FW_UPDATE_END trailer) check it again.
const FW_UNITS={2:"base",3:"controller"};
const FW_MAX=512*1024;  // http::kMaxFirmwareBody
const FW={sec:null,cards:{}};
const CRC_T=(()=>{const t=new Uint32Array(256);for(let n=0;n<256;n++){let c=n;for(let k=0;k<8;k++)c=c&1?0xEDB88320^(c>>>1):c>>>1;t[n]=c>>>0;}return t;})();
function crc32(b){let c=0xFFFFFFFF;for(let i=0;i<b.length;i++)c=CRC_T[(c^b[i])&255]^(c>>>8);return (c^0xFFFFFFFF)>>>0;}
// The trailer (24 B at the end): imageSize, imageCrc, version, boardId, format, reserved, trailerCrc, magic.
function fwInspect(buf,board){
  const b=new Uint8Array(buf),dv=new DataView(buf),use="Use the <name>-update.bin from the firmware's build folder.";
  if(!b.length)return {error:"The file is empty."};
  if(b[0]===0x7F&&b[1]===0x45&&b[2]===0x4C&&b[3]===0x46)return {error:"This is an ELF file. "+use};
  if(b[0]===0x3A)return {error:"This looks like an Intel hex file. "+use};
  if(b.length<24||dv.getUint32(b.length-4,true)!==0x4D43544C)return {error:"No update trailer: this looks like a plain .bin. "+use};
  const t=b.length-24;
  if(crc32(b.subarray(t,t+16))!==dv.getUint32(t+16,true)||b[t+13]!==1)return {error:"The update trailer is corrupt or of an unknown format."};
  const id=b[t+12],size=dv.getUint32(t,true),v=dv.getUint32(t+8,true);
  const r={board:id,size:b.length,version:(v>>>16)+"."+((v>>>8)&255)+"."+(v&255)};
  if(id!==board)return Object.assign(r,{error:FW_UNITS[id]?"This is the "+FW_UNITS[id]+" firmware, not the "+FW_UNITS[board]+
    " firmware. Drop it on the "+(FW_UNITS[id]==="base"?"Base":"Controller")+" card.":"Built for an unknown unit (board ID "+id+")."});
  if(size!==t)return Object.assign(r,{error:"The image size in the trailer does not match the file."});
  if(crc32(b.subarray(0,size))!==dv.getUint32(t+4,true))return Object.assign(r,{error:"The image CRC does not match: the file is damaged."});
  return r;
}
async function fwPick(C,file){
  C.file=file;C.check=null;C.sent=null;
  if(!file){fwDraw();return;}
  C.check=file.size>FW_MAX?{error:"The file is too large for an update file ("+file.size+" bytes)."}:fwInspect(await file.arrayBuffer(),C.board);
  if(C.file===file)fwDraw();
}
// Why the Flash button is off right now ("" = it may be pressed).
function fwBlocked(C){
  if(f("software","fw_update_state")==="running")return "A firmware update is running.";
  if((f("software","playback")||"idle")!=="idle")return "A song is playing; stop it first.";
  if(!f("base","connected")||f("base","alive")===false)return "The base is not connected.";
  if(C.node==="controller"&&!(baseReportFresh()&&f("base","status_ble")))return "The controller is not connected to the base.";
  return "";
}
async function fwFlash(C){
  const file=C.file,ck=C.check;
  if(!file||!ck||ck.error||fwBlocked(C))return;
  if(!confirm("Flash "+C.name+" firmware "+ck.version+" from "+file.name+"?"+(C.node==="base"?
    " The base restarts; playback and the controller link stop for about a minute.":" The controller restarts when it is done.")))return;
  C.busy=true;C.sent=null;fwDraw();
  try{
    const r=await fetch("/api/firmware/"+C.key,{method:"POST",headers:{"Content-Type":"application/octet-stream"},body:await file.arrayBuffer()});
    let j={};try{j=await r.json();}catch(e){}
    C.sent=r.status===202?{ok:true}:{error:j.error||("Refused ("+r.status+").")};
    if(r.status===202){C.file=null;C.check=null;C.E.input.value="";}
  }catch(e){C.sent={error:"The upload did not reach the player."};}
  C.busy=false;fwDraw();
}
function fwCard(key,node,name,board,fileName){
  const C={key,node,name,board,file:null,check:null,sent:null,busy:false,E:{}},E=C.E;
  E.cur=el("div");
  E.drop=el("label","fw-drop");E.input=el("input");E.input.type="file";E.input.accept=".bin";
  E.input.onchange=()=>fwPick(C,E.input.files[0]||null);
  const hint=el("span");hint.append(document.createTextNode("Drop "),el("b","mono",fileName),document.createTextNode(" here or choose it"));
  E.drop.append(E.input,icon("upload"),hint,el("span","fw-why","from build/ of the "+name+" firmware; .elf, .hex and plain .bin are refused"));
  E.drop.addEventListener("dragover",e=>{e.preventDefault();E.drop.classList.add("over");});
  E.drop.addEventListener("dragleave",()=>E.drop.classList.remove("over"));
  E.drop.addEventListener("drop",e=>{e.preventDefault();E.drop.classList.remove("over");
    if(!C.busy&&e.dataTransfer.files.length)fwPick(C,e.dataTransfer.files[0]);});
  E.file=el("div","fw-file");E.run=el("div","fw-run");
  E.flash=el("button","btn primary","Flash "+name);E.flash.onclick=()=>fwFlash(C);
  E.clear=el("button","btn quiet","Clear");E.clear.onclick=()=>{E.input.value="";fwPick(C,null);};
  E.why=el("span","fw-why");E.act=el("div","fw-act");E.act.append(E.flash,E.clear,E.why);
  E.state=el("span","badge");
  const body=el("div","pb");body.append(E.cur,E.drop,E.file,E.act,E.run);
  C.panel=panel((name==="base"?"Base":"Controller")+" firmware",body,E.state);
  FW.cards[key]=C;
  return C.panel;
}
function fwDrawCard(C){
  const E=C.E,n=C.node,rev=hasInfo(n)?f(n,"fw_revision"):null;
  E.cur.replaceChildren(blockTable([{label:"Running now",value:rev?rev+(f(n,"fw_branch")?" ("+f(n,"fw_branch")+")":"")+(f(n,"fw_dirty")?" · uncommitted changes":""):"not reported",mono:true},
    {label:"Built",value:hasInfo(n)?f(n,"fw_built"):null,mono:true}]));
  // The file, once read: what the trailer says, or why it is refused.
  E.file.replaceChildren();
  const ck=C.check;
  if(C.file&&ck){
    const rows=[{label:"File",value:C.file.name,mono:true}];
    if(ck.version)rows.push({label:"Built for",value:FW_UNITS[ck.board]||"unknown unit"},{label:"Version / size",value:ck.version+" · "+ck.size+" bytes"});
    if(!ck.error)rows.push({label:"Check",value:"trailer, board ID and CRC OK"});
    E.file.append(blockTable(rows));
    if(ck.error){const x=el("div","fw-err");x.setAttribute("role","alert");x.append(icon("alert"),el("span",null,ck.error));E.file.append(x);}
  }else if(C.file)E.file.append(el("div","empty","Reading "+C.file.name+"…"));
  if(C.sent&&C.sent.error){const x=el("div","fw-err");x.setAttribute("role","alert");x.append(icon("alert"),el("span",null,"Not flashed: "+C.sent.error));E.file.append(x);}
  // Progress of the update the service runs (also one started with `update` in the terminal).
  const st=f("software","fw_update_state"),mine=f("software","fw_update_target")===C.key;
  E.run.replaceChildren();
  if(st&&mine){
    const pct=f("software","fw_update_pct")||0,msg=f("software","fw_update_msg");
    if(st==="running"){
      const bar=el("div","fw-bar"),i=el("i");i.style.width=pct+"%";bar.append(i);
      bar.setAttribute("role","progressbar");bar.setAttribute("aria-valuenow",pct);bar.setAttribute("aria-valuemin",0);bar.setAttribute("aria-valuemax",100);
      E.run.append(bar,el("div","fw-msg","Flashing "+pct+" %"+(msg?" · "+msg:"")));
    }else E.run.append(el("div","fw-msg",(st==="done"?"Last update finished ":"Last update FAILED ")+ago(f("software","fw_update_ms"))+(msg?" · "+msg:"")));
  }
  const busy=st==="running";
  E.state.className="badge"+(busy&&mine?" warn":mine&&st==="failed"?" bad":mine&&st==="done"?" ok":"");
  E.state.textContent=busy&&mine?"flashing":mine&&st==="done"?"updated":mine&&st==="failed"?"update failed":"";
  E.state.hidden=!E.state.textContent;
  const why=C.busy?"Uploading…":fwBlocked(C);
  E.drop.classList.toggle("off",busy||C.busy);E.input.disabled=busy||C.busy;
  E.flash.disabled=!C.file||!ck||!!ck.error||!!why;
  E.clear.disabled=!C.file||C.busy;
  E.why.textContent=C.file&&ck&&!ck.error?why:"";
}
function fwDraw(){if(FW.sec&&data)for(const C of Object.values(FW.cards))fwDrawCard(C);}
function firmware(){
  if(!FW.sec){
    FW.sec=el("div","grid");
    FW.sec.append(fwCard("base","base","base",2,"midicontrol-fw-update.bin"),
                  fwCard("remote","controller","controller",3,"midicontroller-fw-update.bin"));
  }
  fwDraw();
  return FW.sec;
}

// ================= Home (Czech) =================
function notices(){
  const out=[],add=(red,text,href)=>out.push({red,text,href});
  if(failed)add(true,"Přehrávač neodpovídá, údaje na stránce nemusí být aktuální.",null);
  if(!data)return out;
  const b="base";
  if(f(b,"updating"))add(false,"Probíhá aktualizace firmwaru základny.","#adv/base");
  else if(!f(b,"connected"))add(false,"Základna není připojena. Zkontrolujte USB kabel mezi počítačem a základnou.","#adv/base");
  else if(f(b,"alive")===false)add(true,"Základna neodpovídá.","#adv/base");
  if(f(b,"organ_power_fault"))add(true,"Varhany se nepodařilo vypnout ani po opakování. Zkontrolujte, zda jsou varhany vypnuté, a dejte vědět správci.","#adv/base");
  // A firmware fault resets the device; it recovers by itself, so the organist only needs to pass it on.
  if(data.nodes.base.unseen_faults>0)add(false,"Základna se po poruše firmwaru sama restartovala. Pokud vše hraje, stačí to říct správci.","#adv/diag");
  if(data.nodes.controller.unseen_faults>0)add(false,"Ovladač se po poruše firmwaru sám restartoval. Pokud vše hraje, stačí to říct správci.","#adv/diag");
  if(f("controller","battery_critical"))add(true,"Baterie ovladače je vybitá. Nabijte ho.","#adv/controller");
  else if(battLow("controller"))add(false,"Baterie ovladače je slabá. Nabijte ho.","#adv/controller");
  const bad=data.songs.filter(s=>s.problem).map(s=>s.code);
  if(bad.length)add(false,(bad.length===1?"Soubor písně "+bad[0]+" má chybu a nemusí hrát správně."
    :"Soubory písní "+bad.join(", ")+" mají chybu a nemusí hrát správně.")+" Dejte vědět správci.","#adv/software");
  return out;
}
function noticeList(list){
  const w=el("div","notes");
  for(const n of list){
    const r=el("div","note"+(n.red?" red":""));r.setAttribute("role","status");
    r.append(icon("alert"),el("span",null,n.text));
    if(n.href){const a=el("a",null,"Podrobnosti");a.href=n.href;a.append(icon("chev"));r.append(a);}
    w.append(r);
  }
  return w;
}
const HEALTH={green:"V pořádku",yellow:"Vyžaduje pozornost",red:"Chyba",grey:"Nedostupné"};
function node(ic,name,color,state,fact,href){
  const n=el("a","node"),b=el("div","badge-ic "+color);n.href=href;n.title="Podrobnosti: "+name;
  b.append(icon(ic),dot(color));b.title=HEALTH[color]||"";
  n.append(b,el("div","nm",name),el("div","st "+color,state));
  if(fact)n.append(fact);
  return n;
}
function link(label,state){const l=el("div","link");l.append(el("span","lb",label),el("div","wire "+state));return l;}
function fact(ic,text){const x=el("div","fact");if(ic)x.append(typeof ic==="string"?icon(ic):ic);x.append(el("span",null,text));return x;}
function battery(p,cls){const b=el("span","batt "+cls),i=el("i");i.style.width=Math.max(0,Math.min(100,p))+"%";b.append(i);return b;}
// last_end is the player's English text ("ended normally", "stopped", "stopped (base disconnected)",
// "failed: <why>"); the home page shows Czech only, the raw text stays on the Software page.
function endText(le){
  le=le||"";
  if(le.startsWith("ended"))return "dohrála";
  if(le.startsWith("stopped (base"))return "zastavena, základna se odpojila";
  if(le.startsWith("stopped"))return "zastavena";
  if(le.startsWith("failed"))return "nepodařilo se ji přehrát";
  return le?"skončila":"";
}
function systemPanel(list){
  const sw=data.nodes.software,bs=data.nodes.base,ct=data.nodes.controller;
  // Počítač
  const st=f("software","started_ms");
  const pc=node("pc","Počítač",sw.dot==="red"?"red":"green","Přehrávač běží",
    fact(null,(st!=null?"spuštěn "+dur(serverNow()-st)+" · ":"")+cz(f("software","song_count")||0,"píseň","písně","písní")),"#adv/software");
  // Základna
  const conn=f("base","connected"),alive=f("base","alive")!==false;
  let bState=f("base","updating")?"Aktualizace firmwaru":!conn?"Nepřipojena":!alive?"Neodpovídá":"Připojena";
  // An amber or red ring names its reason, so the word and the colour never disagree.
  if(conn&&alive&&!f("base","updating")){
    const why=[];
    if(bs.unseen_faults>0)why.push("porucha firmwaru");
    if(f("base","midi_port_found")===false)why.push("bez MIDI portu");
    if(f("base","status_answered")===false)why.push("neodpovídá na dotazy");
    if(why.length)bState+=" · "+why.join(", ");
  }
  const op=f("base","organ_power");
  const bs1=node("base","Základna",conn?bs.dot:"grey",bState,
    fact("power",op==null?"Varhany: neznámo":op?"Varhany zapnuty"+(f("base","organ_idle_off_armed")?" (časovač vypnutí běží)":""):"Varhany vypnuty"),"#adv/base");
  // Ovladač (seen through the base's report)
  const fresh=baseReportFresh(),ble=fresh&&f("base","status_ble");
  let cState=!conn||!fresh?"Stav neznámý":ble?"Připojen":"Nepřipojen";
  const bp=battPct("controller"),crit=f("controller","battery_critical");
  let cf=null;
  if(bp!=null){
    const low=crit?"crit":battLow("controller")?"low":"";
    cf=fact(battery(bp,low),"Baterie "+bp+" %"+(f("controller","charging")?", nabíjí se":crit?", vybitá":""));
  }
  const cColor=!conn||!fresh?"grey":ble?ct.dot:(ct.dot==="green"?"grey":ct.dot);
  if(cColor!=="grey"&&cColor!=="green"){
    const why=[];
    if(ct.unseen_faults>0)why.push("porucha firmwaru");
    if(crit)why.push("vybitá baterie");else if(battLow("controller"))why.push("slabá baterie");
    if(why.length)cState+=" · "+why.join(", ");
  }
  const rc=node("remote","Ovladač",cColor,cState,cf,"#adv/controller");
  const usb=!conn?"off":alive?"on":"bad";
  const topo=el("div","topo");topo.append(pc,link("USB",usb),bs1,link("BLUETOOTH",ble?"on":"off"),rc);
  // Právě hraje
  const now=el("div","now"),p=f("software","playback")||"idle";
  const k=el("span","k");
  if(p!=="idle"){
    k.append(el("span","pulse"),document.createTextNode(p==="playing"?"Právě hraje":"Připravuji varhany"));
    now.append(k,el("span","v","Píseň "+(f("software","song")||"")));
    if(f("software","section"))now.append(el("span","sub",f("software","section")));
  }else{
    k.append(icon("note"),document.createTextNode("Nic nehraje"));now.append(k);
    const last=f("software","last_song");
    if(last){
      const end=endText(f("software","last_end"));
      now.append(el("span","sub","naposledy píseň "+last+(end?" ("+end+")":"")));
    }
  }
  const ok=!list.length;
  const aside=el("span","badge "+(ok?"ok":list.some(n=>n.red)?"bad":"warn"));
  aside.append(icon(ok?"check":"alert"),document.createTextNode(ok?"Vše v pořádku":cz(list.length,"upozornění","upozornění","upozornění")));
  aside.firstChild.style.cssText="width:15px;height:15px";
  const pn=panel("Systém",topo,aside);pn.append(now);
  if(failed)pn.classList.add("stale");  // last known state, the player is not answering
  return pn;
}
const HOME={sec:null,status:null};
function home(){
  if(!HOME.sec){HOME.sec=el("div","home");HOME.status=el("div","home");HOME.sec.append(HOME.status,stops());}
  const list=notices(),parts=[];
  if(list.length)parts.push(noticeList(list));
  parts.push(data?systemPanel(list.filter(n=>n.href)):panel("Systém",el("div","skel","Načítám stav systému…")));
  HOME.status.replaceChildren(...parts);
  return HOME.sec;
}

// --- Stops: built once and cached (the 1 s status poll must not rebuild it or lose edits).
// /api/stops is loaded when the page is first opened and after each save, never polled. ---
const SP={sec:null,E:{},tabs:{},loaded:false,loadErr:false,layout:"",groups:[],names:{},codes:[],saved:{},total:0,
  cur:null,draft:new Set(),dirty:false,edits:0,busy:false,sortK:"code",filter:"all",query:"",revealed:false};
function spSorted(set){return [...set].sort((a,b)=>a-b);}
async function spFetch(url,opts){
  const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),5000);
  try{
    const r=await fetch(url,Object.assign({cache:"no-store",signal:ac.signal},opts));
    if(!r.ok){
      const why={400:"chybný požadavek",404:"nenalezeno",415:"nepodporovaný formát",500:"chyba přehrávače při ukládání",
        409:"přehrávač je zaneprázdněn",413:"příliš velká data"}[r.status];
      throw new Error(why||"chyba "+r.status);
    }
    return await r.json();
  }catch(e){throw e.name==="AbortError"?new Error("přehrávač neodpovídá")
    :e instanceof TypeError?new Error("spojení s přehrávačem selhalo"):e;}
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
    const b=el("button","btn sm","Zkusit znovu");b.onclick=spLoad;
    E.load.replaceChildren(el("span","sp-err","Rejstříky se nepodařilo načíst: "+e.message+" "),b);
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
      t.type="button";t.title=n+" "+name;t.setAttribute("aria-label",n+" "+name);
      t.append(el("span","sp-num",n),el("span","sp-name",name));
      t.onclick=()=>{if(SP.draft.has(n))SP.draft.delete(n);else SP.draft.add(n);spChanged();};
      SP.tabs[n]=t;row.append(t);
    }
    col.append(el("div","sp-glabel",g.label),row);p.append(col);
  }
}
function spChanged(){SP.dirty=true;SP.edits++;spMsg("");spDraw();}
function spDraw(){
  const E=SP.E,cur=SP.cur,saved=cur!=null?SP.saved[cur]:undefined,has=cur!=null;
  E.panel.hidden=E.actions.hidden=!has;
  E.cur.textContent=has?"Píseň "+cur:SP.loaded?"Ve složce nejsou žádné písně":SP.loadErr?"Rejstříky nenačteny":"Načítám rejstříky…";
  E.count.textContent="";E.state.hidden=!has;
  if(has){
    if(SP.dirty){E.state.textContent="neuložené změny";E.state.className="badge warn";}
    else if(saved===null){E.state.textContent="nenastaveno: varhany zůstanou, jak jsou";E.state.className="badge";}
    else{E.state.textContent=saved.length?"uloženo":"uloženo: vše vypnuto";E.state.className="badge ok";}
    if(SP.dirty||saved!==null)E.count.textContent=SP.draft.size+" z "+SP.total+" zapnuto";
  }
  E.panel.classList.toggle("notset",saved===null&&!SP.dirty);
  for(const [n,t] of Object.entries(SP.tabs))t.setAttribute("aria-pressed",SP.draft.has(Number(n))?"true":"false");
  E.actions.classList.toggle("dirty",SP.dirty);
  E.save.disabled=!SP.dirty||SP.busy;
  E.alloff.disabled=SP.busy;
  E.notset.disabled=(saved===null&&!SP.dirty)||SP.busy;
  spDrawSongs();
}
function spCmp(a,b){
  const byCode=a.code.localeCompare(b.code,undefined,{numeric:true});
  if(SP.sortK==="code-desc")return -byCode;
  if(SP.sortK==="many")return b.n-a.n||byCode;
  if(SP.sortK==="few")return a.n-b.n||byCode;  // "not set" (-1) first
  return byCode;
}
// Songs after the search box and the filter, sorted.
function spRows(){
  const q=SP.query.trim().toUpperCase(),rows=[];
  for(const c of SP.codes){
    const v=SP.saved[c];
    if(q&&!c.toUpperCase().includes(q))continue;
    if(SP.filter==="set"&&v===null||SP.filter==="unset"&&v!==null)continue;
    rows.push({code:c,v:v===null?null:spSorted(v),n:v===null?-1:v.length});
  }
  return rows.sort(spCmp);
}
function spDrawSongs(){
  const E=SP.E,a=document.activeElement,focus=E.list.contains(a)?a.dataset.code:null,top=E.list.scrollTop;
  const rows=spRows();
  E.list.replaceChildren();
  for(const r of rows){
    const li=el("li","sp-row"+(r.code===SP.cur?" sel":""));
    li.tabIndex=0;li.dataset.code=r.code;li.setAttribute("role","option");
    li.setAttribute("aria-selected",r.code===SP.cur?"true":"false");
    const cnt=el("span","cnt"),list=el("span","list");
    if(r.v===null){cnt.append(el("span","sp-none","nenastaveno"));}
    else if(!r.v.length){cnt.textContent="vše vypnuto";}
    else{cnt.textContent=cz(r.n,"rejstřík","rejstříky","rejstříků");list.textContent=r.v.join(" ");}
    li.append(el("span","code",r.code),cnt);
    if(r.v&&r.v.length)li.append(list);
    li.onclick=()=>spPick(r.code);
    li.onkeydown=e=>{
      if(e.key==="Enter"||e.key===" "){e.preventDefault();spPick(r.code);}
      else if(e.key==="ArrowDown"||e.key==="ArrowUp"){
        e.preventDefault();const s=e.key==="ArrowDown"?li.nextElementSibling:li.previousElementSibling;
        if(s)s.focus();else if(e.key==="ArrowUp")E.search.focus();
      }
    };
    E.list.append(li);
    if(r.code===focus)li.focus();  // keyboard selection keeps its place
  }
  if(!rows.length&&SP.loaded)E.list.append(el("li","sp-empty",SP.codes.length?"Žádná píseň neodpovídá výběru.":"Ve složce nejsou žádné písně."));
  E.list.scrollTop=top;  // a redraw (each stop click) must not jump the list back to the top
  const set=SP.codes.filter(c=>SP.saved[c]!==null).length;
  E.foot.textContent=(rows.length<SP.codes.length?"Zobrazeno "+rows.length+" z "+SP.codes.length:cz(SP.codes.length,"píseň","písně","písní"))+
    " · nastaveno "+set;
  for(const b of E.filters)b.setAttribute("aria-pressed",b.dataset.k===SP.filter?"true":"false");
  if(!SP.revealed&&SP.cur!=null&&rows.length){SP.revealed=true;spReveal();}
}
// Scroll the list (not the page) so the selected song is visible.
function spReveal(){
  const L=SP.E.list,li=[...L.children].find(x=>x.dataset.code===SP.cur);
  if(li&&(li.offsetTop<L.scrollTop||li.offsetTop+li.offsetHeight>L.scrollTop+L.clientHeight))L.scrollTop=li.offsetTop-L.clientHeight/3;
}
function spPick(code){
  if(code===SP.cur)return;
  if(SP.dirty&&!confirm("Zahodit neuložené změny písně "+SP.cur+"?"))return;
  SP.cur=code;SP.draft=new Set(SP.saved[code]||[]);SP.dirty=false;spMsg("");spDraw();spReveal();
  // on a narrow screen the list sits above the editor: bring the editor into view
  if(matchMedia("(max-width:1100px)").matches)SP.E.cur.scrollIntoView({behavior:"smooth",block:"start"});
}
// PUT one song (stops = array, or null = not set). A failure keeps the edits and shows the reason.
async function spPut(stops){
  const code=SP.cur,edits=SP.edits;
  const hadFocus=SP.E.actions.contains(document.activeElement);  // the busy buttons are about to be disabled
  SP.busy=true;spMsg("Ukládám…");spDraw();
  let ok=false;
  try{
    const j=await spFetch("/api/stops/"+encodeURIComponent(code),
      {method:"PUT",headers:{"Content-Type":"application/json"},body:JSON.stringify({stops})});
    SP.saved[code]=j.stops;
    if(SP.cur===code&&SP.edits===edits){SP.dirty=false;SP.draft=new Set(j.stops||[]);}
    ok=true;spMsg("");
  }catch(e){spMsg("Neuloženo: "+e.message,true);}
  SP.busy=false;spDraw();
  // A disabled button drops focus to the page: keep it near the work (the error, else the song's first lit tab).
  if(hadFocus&&!SP.sec.contains(document.activeElement)){
    const t=Object.values(SP.tabs);
    (ok?t.find(x=>x.getAttribute("aria-pressed")==="true")||t[0]:SP.E.msg).focus();
  }
  if(ok)spLoad();
  return ok;
}
function stops(){
  if(SP.sec)return SP.sec;
  const E=SP.E,body=el("div","sp-body");
  // song list
  const side=el("div","sp-side"),tools=el("div","sp-tools"),search=el("label","sp-search"),seg=el("div","seg full");
  E.search=el("input");E.search.type="search";E.search.placeholder="Hledat píseň…";E.search.setAttribute("aria-label","Hledat píseň podle čísla");
  E.search.inputMode="search";E.search.autocomplete="off";
  E.search.oninput=()=>{SP.query=E.search.value;SP.E.list.scrollTop=0;spDrawSongs();};
  E.search.onkeydown=e=>{  // Enter opens the first match, arrow down walks into the list
    const first=E.list.querySelector(".sp-row");
    if(e.key==="Enter"&&first){e.preventDefault();spPick(first.dataset.code);}
    else if(e.key==="ArrowDown"&&first){e.preventDefault();first.focus();}
  };
  search.append(icon("search"),E.search);
  E.filters=[];
  for(const [k,name] of [["all","Vše"],["set","Nastavené"],["unset","Nenastavené"]]){
    const b=el("button",null,name);b.type="button";b.dataset.k=k;
    b.onclick=()=>{SP.filter=k;SP.E.list.scrollTop=0;spDrawSongs();};
    seg.append(b);E.filters.push(b);
  }
  const sort=el("select");sort.setAttribute("aria-label","Řazení");
  for(const [k,name] of [["code","Řadit: číslo vzestupně"],["code-desc","Řadit: číslo sestupně"],["many","Řadit: nejvíc rejstříků"],["few","Řadit: nenastavené a nejméně rejstříků"]]){
    const o=el("option",null,name);o.value=k;sort.append(o);
  }
  sort.onchange=()=>{SP.sortK=sort.value;SP.E.list.scrollTop=0;spDrawSongs();};
  tools.append(search,seg,sort);
  E.list=el("ul","sp-songs");E.list.setAttribute("role","listbox");E.list.setAttribute("aria-label","Písně");
  E.foot=el("div","sp-foot");
  side.append(tools,E.list,E.foot);
  // editor
  const main=el("div","sp-main"),head=el("div","sp-head");
  E.cur=el("h3","sp-cur");E.count=el("span","sp-count");E.state=el("span","badge");
  head.append(E.cur,E.state,E.count);
  E.load=el("div","sp-load");E.panel=el("div","sp-panel");E.actions=el("div","sp-actions");
  E.save=el("button","btn primary","Uložit");E.alloff=el("button","btn","Vše vypnout");E.notset=el("button","btn quiet","Zrušit nastavení");
  E.msg=el("span","sp-msg");E.msg.setAttribute("role","status");E.msg.tabIndex=-1;
  E.save.onclick=async()=>{
    if(await spPut(spSorted(SP.draft))){E.save.textContent="Uloženo";setTimeout(()=>{E.save.textContent="Uložit";},1200);}
  };
  E.alloff.onclick=()=>{if(SP.draft.size||SP.saved[SP.cur]===null){SP.draft.clear();spChanged();}};
  E.notset.onclick=()=>{
    if(confirm("Zrušit nastavení rejstříků písně "+SP.cur+"?"+(SP.dirty?" Neuložené změny se také zahodí.":"")+
               " Při spuštění této písně pak varhany zůstanou, jak jsou."))spPut(null);
  };
  E.notset.title="Smaže uložený výběr: při spuštění písně se rejstříky nemění";
  E.actions.append(E.save,E.alloff,E.msg,E.notset);
  main.append(head,el("p","sp-hint","Na začátku písně přehrávač vypne všechny rejstříky a zapne vybrané. Píseň bez nastavení nechá varhany beze změny."),
              E.load,E.panel,E.actions);
  body.append(side,main);
  const s=panel("Rejstříky písní",body);s.id="stops";
  SP.sec=s;spDraw();spLoad();
  return s;
}

// ================= page =================
// Hash: "" home, "#stops" home at the stops editor, "#adv/<page>" Advanced; the old "#base" etc. still work.
function route(){
  let h=location.hash.slice(1),adv=false;
  if(h==="adv"||h.startsWith("adv/")){adv=true;h=h.slice(4);}
  if(ADV.some(x=>x[0]===h)&&(adv||h!=="overview"))return {view:"adv",tab:h};
  if(adv)return {view:"adv",tab:"overview"};
  return {view:"home",tab:h==="stops"?"stops":""};
}
function renderNav(r){
  const nav=document.getElementById("views");
  if(!nav.firstChild)for(const [k,name,href] of [["home","Přehled","#"],["adv","Pokročilé","#adv"]]){
    const a=el("a",null,name);a.href=href;a.dataset.k=k;nav.append(a);
  }
  for(const a of nav.children)if(a.dataset.k===r.view)a.setAttribute("aria-current","page");else a.removeAttribute("aria-current");
}
const AV={sec:null,nav:null,title:null,slot:null};
function advanced(tab){
  if(!AV.sec){
    AV.sec=el("div","adv");AV.nav=el("nav","subnav");AV.nav.setAttribute("aria-label","Advanced");
    const body=el("div","adv-body");AV.title=el("h1","adv-title");AV.slot=el("div");
    body.append(AV.title,AV.slot);AV.sec.append(AV.nav,body);AV.sec.lang="en";
  }
  AV.nav.replaceChildren();
  for(const [k,name] of ADV){
    const a=el("a",null,name);a.href="#adv/"+k;
    if(k===tab)a.setAttribute("aria-current","page");
    if(data&&data.nodes[k])a.append(dot(data.nodes[k].dot));
    if(k==="diag"&&data&&data.nodes.base.unseen_faults+data.nodes.controller.unseen_faults>0)a.append(dot("yellow"));
    AV.nav.append(a);
  }
  AV.title.textContent=ADV.find(x=>x[0]===tab)[1];
  if(!data){AV.slot.replaceChildren(el("div","empty","Loading…"));return AV.sec;}
  const sec={overview,software,base,controller,diag,firmware}[tab]();
  if(AV.slot.firstChild!==sec)AV.slot.replaceChildren(sec);  // the cached Diagnostics page keeps focus
  return AV.sec;
}
function render(){
  const r=route();renderNav(r);
  const sec=r.view==="home"?home():advanced(r.tab);
  const main=document.getElementById("main");
  if(main.firstChild!==sec)main.replaceChildren(sec);  // the cached sections keep focus and stop edits
}
function renderTop(){
  const u=document.getElementById("upd");u.replaceChildren();
  u.className="conn"+(failed?" bad":"");
  u.append(dot(failed?"red":data?"green":"grey"));
  let s=failed?"bez spojení s přehrávačem":data?"aktualizováno před "+((Date.now()-fetchedAt)/1000).toFixed(0)+" s":"připojování…";
  u.append(el("span",failed?null:"tx",s));
  if(!failed&&data)u.append(el("span","short","připojeno"));
  u.title=location.host;
}
async function poll(){
  // Give up after 3 s, so a frozen service shows as no connection.
  const ac=new AbortController(),timer=setTimeout(()=>ac.abort(),3000);
  try{
    const r=await fetch("/api/status",{cache:"no-store",signal:ac.signal});
    if(!r.ok)throw new Error(r.status);
    data=await r.json();fetchedAt=Date.now();failed=false;
  }catch(e){failed=true;}
  clearTimeout(timer);
  // Don't redraw while the viewer is selecting text.
  if(String(window.getSelection())==="")render();
  // "#stops" opened directly: jump again once the system panel above has its real height.
  if(data&&!poll.landed){poll.landed=true;if(route().tab==="stops")SP.sec.scrollIntoView();}
  renderTop();
  setTimeout(poll,1000);
}
window.addEventListener("hashchange",()=>{render();const r=route();
  if(r.tab==="stops")SP.sec.scrollIntoView({behavior:"smooth"});else window.scrollTo(0,0);});
// Reloading or closing the page with unsaved stop edits asks first.
window.addEventListener("beforeunload",e=>{if(SP.dirty){e.preventDefault();e.returnValue="";}});
document.getElementById("mark").append(icon("organ"));
setInterval(renderTop,500);
render();
if(route().tab==="stops")SP.sec.scrollIntoView();
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
