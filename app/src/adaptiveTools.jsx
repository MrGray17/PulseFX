import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Activity, Gauge, RadioTower, SlidersHorizontal, Sparkles, X } from 'lucide-react';
import { pulsefxApi } from './pulsefxApi.js';
import './adaptiveTools.css';

const STORAGE_KEY = 'pulsefx.adaptiveTools.v1';
const CONTENTS = ['general', 'music', 'movie', 'game', 'voice'];
const DEFAULT_CALIBRATION = { enabled: false, itdScale: 1, ipsilateralGain: 1, contralateralGain: 1, wetTrimDb: 0 };
const DEFAULT_STATE = {
  version: 1,
  sceneAutomation: true,
  defaultContent: 'general',
  lowLatency: false,
  signatureStrength: 1,
  rules: [],
  calibrations: {},
};

function clamp(value, min, max, fallback = min) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(max, Math.max(min, number)) : fallback;
}

function normalizeProcessKey(value) {
  const text = String(value ?? '').trim().replaceAll('\\', '/');
  const key = text.slice(text.lastIndexOf('/') + 1).toLocaleLowerCase();
  return key.slice(0, 260);
}

function safeCalibration(value) {
  const source = value && typeof value === 'object' ? value : {};
  return {
    enabled: Boolean(source.enabled),
    itdScale: clamp(source.itdScale, 0.75, 1.25, 1),
    ipsilateralGain: clamp(source.ipsilateralGain, 0.65, 1.20, 1),
    contralateralGain: clamp(source.contralateralGain, 0.45, 1.15, 1),
    wetTrimDb: clamp(source.wetTrimDb, -6, 1.5, 0),
  };
}

function loadState() {
  try {
    const parsed = JSON.parse(localStorage.getItem(STORAGE_KEY) || '{}');
    const rules = Array.isArray(parsed.rules) ? parsed.rules.slice(0, 128).map((rule) => ({
      processKey: normalizeProcessKey(rule?.processKey),
      content: CONTENTS.includes(rule?.content) ? rule.content : 'general',
      lowLatency: Boolean(rule?.lowLatency),
      priority: Math.round(clamp(rule?.priority, 0, 100, 50)),
    })).filter((rule) => rule.processKey) : [];
    const calibrations = {};
    if (parsed.calibrations && typeof parsed.calibrations === 'object') {
      for (const [key, value] of Object.entries(parsed.calibrations).slice(0, 256)) {
        if (key.length > 1024) continue;
        calibrations[key] = safeCalibration(value);
      }
    }
    return {
      ...DEFAULT_STATE,
      sceneAutomation: typeof parsed.sceneAutomation === 'boolean' ? parsed.sceneAutomation : true,
      defaultContent: CONTENTS.includes(parsed.defaultContent) ? parsed.defaultContent : 'general',
      lowLatency: Boolean(parsed.lowLatency),
      signatureStrength: clamp(parsed.signatureStrength, 0.5, 1.25, 1),
      rules,
      calibrations,
    };
  } catch {
    return { ...DEFAULT_STATE };
  }
}

function saveState(state) {
  try { localStorage.setItem(STORAGE_KEY, JSON.stringify({ ...state, version: 1 })); } catch {}
}

function headphoneIdentity(settings) {
  const path = typeof settings?.headphoneModelPath === 'string' && settings.headphoneModelPath ? settings.headphoneModelPath : 'default-output';
  const name = typeof settings?.headphoneModelName === 'string' && settings.headphoneModelName ? settings.headphoneModelName : 'Default / unknown headphones';
  return { key: path, name };
}

async function command(name, ...args) {
  try { return await pulsefxApi.command(name, ...args); } catch { return null; }
}

async function applyCalibration(calibration) {
  const safe = safeCalibration(calibration);
  return command(
    'spatial_calibration',
    safe.enabled,
    safe.itdScale,
    safe.ipsilateralGain,
    safe.contralateralGain,
    safe.wetTrimDb,
  );
}

async function playSpatialProbe() {
  if (typeof AudioContext === 'undefined') return false;
  const context = new AudioContext();
  try {
    await context.resume();
    const seconds = 2.4;
    const frames = Math.floor(context.sampleRate * seconds);
    const buffer = context.createBuffer(2, frames, context.sampleRate);
    const left = buffer.getChannelData(0);
    const right = buffer.getChannelData(1);
    let seed = 0x50554658;
    const random = () => {
      seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5;
      return ((seed >>> 0) / 0xffffffff) * 2 - 1;
    };
    for (let frame = 0; frame < frames; frame += 1) {
      const time = frame / context.sampleRate;
      const segment = Math.min(5, Math.floor(time / 0.4));
      const local = (time % 0.4) / 0.4;
      const envelope = Math.sin(Math.PI * Math.min(1, local)) ** 2;
      const tone = Math.sin(2 * Math.PI * (segment % 2 ? 997 : 641) * time);
      const noise = random() * 0.055;
      const sample = (tone * 0.10 + noise) * envelope;
      const pan = [-0.92, -0.45, 0, 0.45, 0.92, 0][segment];
      const angle = (pan + 1) * Math.PI / 4;
      left[frame] = sample * Math.cos(angle);
      right[frame] = sample * Math.sin(angle);
    }
    const source = context.createBufferSource();
    source.buffer = buffer;
    source.connect(context.destination);
    source.start();
    await new Promise((resolve) => { source.onended = resolve; });
    return true;
  } finally {
    context.close().catch(() => {});
  }
}

function AdaptiveTools() {
  const [open, setOpen] = useState(false);
  const [tab, setTab] = useState('scenes');
  const [state, setState] = useState(loadState);
  const [apps, setApps] = useState([]);
  const [status, setStatus] = useState(null);
  const [headphone, setHeadphone] = useState({ key: 'default-output', name: 'Default / unknown headphones' });
  const [calibrationDraft, setCalibrationDraft] = useState(DEFAULT_CALIBRATION);
  const [probeBusy, setProbeBusy] = useState(false);
  const [abx, setAbx] = useState({ total: 0, correct: 0, hidden: 'personalized', played: false });
  const stateRef = useRef(state);
  const headphoneRef = useRef(headphone);

  stateRef.current = state;
  headphoneRef.current = headphone;
  const calibration = useMemo(
    () => safeCalibration(state.calibrations[headphone.key] ?? DEFAULT_CALIBRATION),
    [state.calibrations, headphone.key],
  );

  const persist = (updater) => {
    setState((current) => {
      const next = typeof updater === 'function' ? updater(current) : updater;
      saveState(next);
      return next;
    });
  };

  const replayPolicy = async () => {
    const current = stateRef.current;
    await command('scene_enable', current.sceneAutomation);
    await command('content', current.defaultContent);
    await command('low_latency', current.lowLatency);
    await command('signature_strength', current.signatureStrength);
    await command('scene_clear');
    for (const rule of current.rules) {
      await command('scene_set', rule.processKey, rule.content, rule.lowLatency, rule.priority);
    }
    const currentCalibration = safeCalibration(current.calibrations[headphoneRef.current.key] ?? DEFAULT_CALIBRATION);
    await applyCalibration(currentCalibration);
  };

  useEffect(() => {
    let cancelled = false;
    const refreshHeadphone = async () => {
      const settings = await pulsefxApi.peekSettings();
      if (cancelled) return;
      const next = headphoneIdentity(settings);
      setHeadphone((current) => current.key === next.key && current.name === next.name ? current : next);
    };
    refreshHeadphone();
    const timer = setInterval(refreshHeadphone, 2500);
    const removeHost = pulsefxApi.onHostState((message) => {
      if (message?.running === true) replayPolicy();
    });
    replayPolicy();
    return () => { cancelled = true; clearInterval(timer); removeHost(); };
  }, []);

  useEffect(() => {
    setCalibrationDraft(calibration);
    applyCalibration(calibration);
  }, [headphone.key]);

  useEffect(() => {
    if (!open) return undefined;
    let cancelled = false;
    const refresh = async () => {
      const [nextStatus, nextApps] = await Promise.all([command('status'), command('apps')]);
      if (cancelled) return;
      if (nextStatus?.type === 'status') setStatus(nextStatus);
      if (nextApps?.type === 'apps') setApps(Array.isArray(nextApps.apps) ? nextApps.apps : []);
    };
    refresh();
    const timer = setInterval(refresh, 1000);
    return () => { cancelled = true; clearInterval(timer); };
  }, [open]);

  const updateGlobal = async (key, value, nativeName) => {
    persist((current) => ({ ...current, [key]: value }));
    await command(nativeName, value);
  };

  const setRule = async (app, content) => {
    const processKey = normalizeProcessKey(app.processName || app.name);
    if (!processKey) return;
    if (content === 'none') {
      persist((current) => ({ ...current, rules: current.rules.filter((rule) => rule.processKey !== processKey) }));
      await command('scene_remove', processKey);
      return;
    }
    const existing = state.rules.find((rule) => rule.processKey === processKey);
    const rule = {
      processKey,
      content,
      lowLatency: existing?.lowLatency ?? (content === 'game' || content === 'voice'),
      priority: existing?.priority ?? (content === 'game' ? 90 : content === 'voice' ? 80 : 60),
    };
    persist((current) => ({ ...current, rules: [...current.rules.filter((item) => item.processKey !== processKey), rule].slice(-128) }));
    await command('scene_set', rule.processKey, rule.content, rule.lowLatency, rule.priority);
  };

  const updateRule = async (processKey, patch) => {
    let updated = null;
    persist((current) => ({
      ...current,
      rules: current.rules.map((rule) => {
        if (rule.processKey !== processKey) return rule;
        updated = { ...rule, ...patch };
        return updated;
      }),
    }));
    if (updated) await command('scene_set', updated.processKey, updated.content, updated.lowLatency, updated.priority);
  };

  const saveCalibration = async () => {
    const nextCalibration = { ...safeCalibration(calibrationDraft), enabled: true };
    persist((current) => ({ ...current, calibrations: { ...current.calibrations, [headphone.key]: nextCalibration } }));
    setCalibrationDraft(nextCalibration);
    await applyCalibration(nextCalibration);
  };

  const disableCalibration = async () => {
    const nextCalibration = { ...safeCalibration(calibrationDraft), enabled: false };
    persist((current) => ({ ...current, calibrations: { ...current.calibrations, [headphone.key]: nextCalibration } }));
    setCalibrationDraft(nextCalibration);
    await applyCalibration(nextCalibration);
  };

  const playVariant = async (variant) => {
    if (probeBusy) return false;
    setProbeBusy(true);
    const personalized = { ...safeCalibration(calibrationDraft), enabled: true };
    const audition = variant === 'personalized' ? personalized : { ...personalized, enabled: false };
    const restore = safeCalibration(calibration);
    try {
      await applyCalibration(audition);
      return await playSpatialProbe();
    } finally {
      await applyCalibration(restore);
      setProbeBusy(false);
    }
  };

  const newAbxTrial = () => setAbx((current) => ({ ...current, hidden: Math.random() < 0.5 ? 'default' : 'personalized', played: false }));
  const playAbx = async () => {
    if (probeBusy) return;
    const hidden = abx.hidden;
    await playVariant(hidden);
    setAbx((current) => current.hidden === hidden ? { ...current, played: true } : current);
  };
  const guessAbx = (guess) => {
    setAbx((current) => {
      if (!current.played) return current;
      return {
        total: current.total + 1,
        correct: current.correct + (guess === current.hidden ? 1 : 0),
        hidden: Math.random() < 0.5 ? 'default' : 'personalized',
        played: false,
      };
    });
  };

  const ruleFor = (app) => state.rules.find((rule) => rule.processKey === normalizeProcessKey(app.processName || app.name));
  const stats = status?.stats ?? {};
  const scene = status?.scene ?? {};

  return <>
    <button className="adaptive-tools-launch" aria-label="Open Adaptive Tools" onClick={() => setOpen(true)}><Activity size={15}/><span>Adaptive</span></button>
    {open && <div className="adaptive-overlay" role="presentation" onMouseDown={(event) => { if (event.target === event.currentTarget) setOpen(false); }}>
      <section className="adaptive-panel" role="dialog" aria-modal="true" aria-label="Adaptive Tools">
        <header className="adaptive-header"><div><p>ADAPTIVE ENGINE</p><h2>PulseFX Lab</h2></div><button aria-label="Close Adaptive Tools" onClick={() => setOpen(false)}><X size={18}/></button></header>
        <nav className="adaptive-tabs">
          <button className={tab === 'scenes' ? 'active' : ''} onClick={() => setTab('scenes')}><RadioTower size={15}/>Scenes</button>
          <button className={tab === 'calibration' ? 'active' : ''} onClick={() => setTab('calibration')}><SlidersHorizontal size={15}/>Calibration</button>
          <button className={tab === 'diagnostics' ? 'active' : ''} onClick={() => setTab('diagnostics')}><Gauge size={15}/>Diagnostics</button>
        </nav>

        {tab === 'scenes' && <div className="adaptive-body">
          <div className="adaptive-grid two">
            <label className="adaptive-field"><span>Scene automation</span><input aria-label="Scene automation" type="checkbox" checked={state.sceneAutomation} onChange={(event) => updateGlobal('sceneAutomation', event.target.checked, 'scene_enable')}/></label>
            <label className="adaptive-field"><span>Default content</span><select aria-label="Default content" value={state.defaultContent} onChange={(event) => updateGlobal('defaultContent', event.target.value, 'content')}>{CONTENTS.map((item) => <option key={item}>{item}</option>)}</select></label>
            <label className="adaptive-field"><span>Interactive policy</span><input aria-label="Low latency policy" type="checkbox" checked={state.lowLatency} onChange={(event) => updateGlobal('lowLatency', event.target.checked, 'low_latency')}/><small>De-rates nonessential processing; Diagnostics shows real latency.</small></label>
            <label className="adaptive-field wide"><span>Signature strength <strong>{Math.round(state.signatureStrength * 100)}%</strong></span><input aria-label="Signature strength" type="range" min="0.5" max="1.25" step="0.05" value={state.signatureStrength} onChange={(event) => updateGlobal('signatureStrength', Number(event.target.value), 'signature_strength')}/></label>
          </div>
          <div className="scene-summary"><span className={scene.matched ? 'dot live' : 'dot'}/><strong>{scene.matched ? scene.processName : 'No scene matched'}</strong><small>{scene.matched ? `${scene.content}${scene.lowLatency ? ' · interactive' : ''}` : 'Default Signature policy is active.'}</small></div>
          <div className="adaptive-list">
            {apps.length === 0 && <div className="adaptive-empty">No Windows audio sessions detected.</div>}
            {apps.map((app) => { const rule = ruleFor(app); const key = normalizeProcessKey(app.processName || app.name); return <div className="scene-row" key={`${app.pid}-${key}`}>
              <div><span className={app.active ? 'dot live' : 'dot'}/><strong>{app.name || key}</strong><small>{key} · {app.active ? 'active' : 'idle'}</small></div>
              <select aria-label={`Scene for ${app.name || key}`} value={rule?.content ?? 'none'} onChange={(event) => setRule(app, event.target.value)}><option value="none">No scene</option>{CONTENTS.map((item) => <option key={item} value={item}>{item}</option>)}</select>
              <label><input aria-label={`Low latency for ${app.name || key}`} type="checkbox" disabled={!rule} checked={Boolean(rule?.lowLatency)} onChange={(event) => updateRule(key, { lowLatency: event.target.checked })}/>Fast</label>
              <input aria-label={`Priority for ${app.name || key}`} type="number" disabled={!rule} min="0" max="100" value={rule?.priority ?? 50} onChange={(event) => updateRule(key, { priority: Math.round(clamp(event.target.value, 0, 100, 50)) })}/>
            </div>; })}
          </div>
        </div>}

        {tab === 'calibration' && <div className="adaptive-body">
          <div className="calibration-hero"><Sparkles size={20}/><div><strong>{headphone.name}</strong><span>{headphone.key === 'default-output' ? 'Generic output calibration' : 'Calibration is stored for this exact headphone profile.'}</span></div><span className={calibration.enabled ? 'cal-badge on' : 'cal-badge'}>{calibration.enabled ? 'Personalized' : 'Default'}</span></div>
          <p className="adaptive-note">Tune for a stable center and the strongest believable outside-the-head image. This is listener calibration, not a claim of laboratory ear geometry measurement.</p>
          <div className="calibration-sliders">
            {[
              ['ITD scale','itdScale',0.75,1.25,0.01],['Near-ear energy','ipsilateralGain',0.65,1.20,0.01],['Far-ear / head shadow','contralateralGain',0.45,1.15,0.01],['Spatial trim dB','wetTrimDb',-6,1.5,0.1],
            ].map(([label,key,min,max,step]) => <label key={key}><span>{label}<strong>{Number(calibrationDraft[key]).toFixed(key === 'wetTrimDb' ? 1 : 2)}</strong></span><input aria-label={label} type="range" min={min} max={max} step={step} value={calibrationDraft[key]} onChange={(event) => setCalibrationDraft((current) => ({ ...current, [key]: Number(event.target.value) }))}/></label>)}
          </div>
          <div className="calibration-actions"><button onClick={() => playVariant('default')} disabled={probeBusy}>Play A · Default</button><button onClick={() => playVariant('personalized')} disabled={probeBusy}>Play B · Personalized</button><button className="primary" onClick={saveCalibration}>Save personalized</button><button onClick={disableCalibration}>Use default</button></div>
          <div className="abx-card"><div><strong>Blind X check</strong><span>{abx.total ? `${abx.correct}/${abx.total} correct` : 'Compare without knowing which profile X uses.'}</span></div><button onClick={newAbxTrial} disabled={probeBusy}>New trial</button><button disabled={probeBusy} onClick={playAbx}>Play X</button><button disabled={!abx.played || probeBusy} onClick={() => guessAbx('default')}>X = A</button><button disabled={!abx.played || probeBusy} onClick={() => guessAbx('personalized')}>X = B</button></div>
        </div>}

        {tab === 'diagnostics' && <div className="adaptive-body">
          <div className="diagnostic-grid">
            <div><small>STREAM</small><strong>{stats.sampleRate ? `${stats.sampleRate.toLocaleString()} Hz` : '—'}</strong><span>{stats.inputChannels ? `${stats.inputChannels} input channels` : 'No live stream'}</span></div>
            <div><small>DSP LATENCY</small><strong>{Number(stats.processorLatencyMs ?? 0).toFixed(1)} ms</strong><span>{stats.processorLatencyFrames ?? 0} frames</span></div>
            <div><small>INTERNAL BUFFER + DSP</small><strong>{Number(stats.internalLatencyMs ?? 0).toFixed(1)} ms</strong><span>Does not claim device/air acoustic latency.</span></div>
            <div><small>LIMITER</small><strong>{Number(stats.limiterGainReductionDb ?? 0).toFixed(2)} dB</strong><span>current gain reduction</span></div>
            <div><small>HEADROOM GOVERNOR</small><strong>{Math.round(clamp(stats.headroomStress ?? 0, 0, 1, 0) * 100)}%</strong><span>Signature enrichment pressure</span></div>
            <div><small>MASTER TRANSITION</small><strong>{Math.round(clamp(stats.masterWetMix ?? 0, 0, 1, 0) * 100)}% wet</strong><span>latency-matched dry/wet ramp</span></div>
            <div><small>ROUTING</small><strong>{status?.routingActive ? 'Active' : 'Bypassed'}</strong><span>{status?.error || 'PulseFX Output route'}</span></div>
            <div><small>DROPOUTS</small><strong>{(stats.underruns ?? 0) + (stats.overruns ?? 0)}</strong><span>{stats.underruns ?? 0} under · {stats.overruns ?? 0} over</span></div>
            <div><small>ACTIVE POLICY</small><strong>{status?.controls?.content ?? 'general'}</strong><span>{status?.controls?.lowLatency ? 'interactive policy' : 'quality policy'} · {Math.round((status?.controls?.signatureStrength ?? 1) * 100)}%</span></div>
          </div>
          <p className="adaptive-note">“Internal buffer + DSP” is an engineering metric from PulseFX itself. Final end-to-end latency targets still require physical Windows measurement across actual devices.</p>
        </div>}
      </section>
    </div>}
  </>;
}

const root = document.getElementById('adaptive-tools-root');
if (root) createRoot(root).render(<AdaptiveTools/>);
