import React, { useEffect, useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  AudioLines, ChevronDown, Headphones, Layers3, Moon, Power, Radio,
  SlidersHorizontal, Sparkles, Waves, Volume2, VolumeX, CircleAlert,
} from 'lucide-react';
import { pulsefxApi } from './pulsefxApi.js';
import './styles.css';

const bands = ['20','25','31','40','50','63','80','100','125','160','200','250','315','400','500','630','800','1k','1.25k','1.6k','2k','2.5k','3.15k','4k','5k','6.3k','8k','10k','12.5k','16k','20k'];
const flat = () => bands.map(() => 0);
const presets = {
  Flat: flat(),
  Music: [2,2,2,2,2,2,1,1,1,0,0,0,0,-1,-1,-1,0,1,1,1,1,1,1,2,2,2,2,2,2,1,1],
  Movies: [2,2,2,2,2,1,1,1,0,0,0,0,0,1,1,2,2,2,2,2,2,2,2,1,1,1,1,1,0,0,0],
  Vocal: [-2,-2,-2,-2,-2,-2,-1,-1,-1,0,0,0,1,1,1,2,2,3,3,3,3,2,2,1,1,1,0,0,-1,-1,-1],
};

const effects = [
  { id: 'surround', label: '3D Surround', icon: Layers3, description: 'Binaural HRTF rendering with cross-ear timing and spectral cues.' },
  { id: 'fidelity', label: 'Fidelity', icon: Sparkles, description: 'Adaptive detail lift that favors quieter spectral information.' },
  { id: 'spatial', label: 'Spatial', icon: Waves, description: 'Frequency-aware stereo expansion while keeping low frequencies anchored.' },
  { id: 'ambience', label: 'Ambience', icon: Radio, description: 'Short early reflections that add space without washing out transients.' },
  { id: 'night', label: 'Night Mode', icon: Moon, description: 'Tighter dynamics for clearer quiet detail and less aggressive peaks.' },
];

const incompatible = {
  surround: ['spatial', 'ambience', 'night'],
  spatial: ['surround', 'ambience'],
  ambience: ['surround', 'spatial', 'night'],
  night: ['surround', 'ambience'],
  fidelity: [],
};

const defaultEffectEnabled = { fidelity: true, spatial: true, surround: false, ambience: false, night: false };
const defaultEffectAmounts = { surround: 48, fidelity: 42, spatial: 34, ambience: 30, night: 55 };

function clamp(value, min, max) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(max, Math.max(min, number)) : min;
}

function App() {
  const [enabled, setEnabled] = useState(true);
  const [activeTab, setActiveTab] = useState('enhance');
  const [activeEffect, setActiveEffect] = useState('fidelity');
  const [effectEnabled, setEffectEnabled] = useState(defaultEffectEnabled);
  const [effectAmounts, setEffectAmounts] = useState(defaultEffectAmounts);
  const [preamp, setPreamp] = useState(0);
  const [preset, setPreset] = useState('Music');
  const [eq, setEq] = useState([...presets.Music]);
  const [headphoneEq, setHeadphoneEq] = useState(false);
  const [outputId, setOutputId] = useState('');
  const [devices, setDevices] = useState([]);
  const [apps, setApps] = useState([]);
  const [status, setStatus] = useState({ running: false, error: '', stats: {} });
  const [hostError, setHostError] = useState('');
  const [hydrated, setHydrated] = useState(false);

  const active = effects.find((effect) => effect.id === activeEffect) ?? effects[0];
  const ActiveIcon = active.icon;
  const intensity = effectAmounts[activeEffect] ?? 0;
  const curve = useMemo(() => eq.map((value) => 50 - value * 2.7), [eq]);
  const physicalDevices = devices.filter((device) => !device.pulsefx);

  const run = async (name, ...args) => {
    try {
      const result = await pulsefxApi.command(name, ...args);
      if (result?.ok === false && result.type !== 'offline') throw new Error(result.error || 'PulseFX command failed');
      setHostError(result?.type === 'offline' ? result.error : '');
      return result;
    } catch (error) {
      setHostError(error instanceof Error ? error.message : String(error));
      return null;
    }
  };

  const refreshStatus = async () => {
    const next = await run('status');
    if (next?.type === 'status') {
      setStatus(next);
      if (!outputId && next.destinationId) setOutputId(next.destinationId);
    }
  };

  const refreshDevices = async () => {
    const next = await run('devices');
    if (next?.type === 'devices') setDevices(next.devices ?? []);
  };

  const refreshApps = async () => {
    const next = await run('apps');
    if (next?.type === 'apps') setApps(next.apps ?? []);
  };

  const sendEffect = async (id, on, amount) => {
    const normalized = on ? clamp(amount, 0, 100) / 100 : 0;
    if (id === 'night') {
      await run('night', on);
      await run('dynamics', normalized);
      return;
    }
    await run(id === 'spatial' ? 'spatial' : id, normalized);
  };

  const syncAllControls = async (state) => {
    await run('enabled', state.enabled);
    await run('preamp', state.preamp);
    await run('headphone_enable', state.headphoneEq);
    for (const effect of effects) {
      await sendEffect(effect.id, Boolean(state.effectEnabled[effect.id]), state.effectAmounts[effect.id] ?? 0);
    }
    for (let index = 0; index < state.eq.length; index += 1) await run('eq', index, state.eq[index]);
    if (state.outputId) await run('output', state.outputId);
  };

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const saved = await pulsefxApi.loadSettings();
      if (cancelled) return;
      const restored = {
        enabled: typeof saved.enabled === 'boolean' ? saved.enabled : true,
        preamp: clamp(saved.preamp ?? 0, -12, 9),
        headphoneEq: typeof saved.headphoneEq === 'boolean' ? saved.headphoneEq : false,
        effectEnabled: { ...defaultEffectEnabled, ...(saved.effectEnabled ?? {}) },
        effectAmounts: { ...defaultEffectAmounts, ...(saved.effectAmounts ?? {}) },
        eq: Array.isArray(saved.eq) && saved.eq.length === bands.length ? saved.eq.map((value) => clamp(value, -12, 12)) : [...presets.Music],
        preset: typeof saved.preset === 'string' ? saved.preset : 'Music',
        outputId: typeof saved.outputId === 'string' ? saved.outputId : '',
      };
      setEnabled(restored.enabled);
      setPreamp(restored.preamp);
      setHeadphoneEq(restored.headphoneEq);
      setEffectEnabled(restored.effectEnabled);
      setEffectAmounts(restored.effectAmounts);
      setEq(restored.eq);
      setPreset(restored.preset);
      setOutputId(restored.outputId);
      setHydrated(true);
      await syncAllControls(restored);
      await Promise.all([refreshStatus(), refreshDevices()]);
    })();
    return () => { cancelled = true; };
  }, []);

  useEffect(() => {
    const removeEvent = pulsefxApi.onEvent((message) => {
      if (message?.type === 'status') setStatus(message);
    });
    const removeHostState = pulsefxApi.onHostState((message) => {
      if (message?.running === false) setHostError(message.error || 'PulseFX audio host stopped');
      if (message?.running === true) setHostError('');
    });
    return () => { removeEvent(); removeHostState(); };
  }, []);

  useEffect(() => {
    if (!hydrated) return undefined;
    const timer = setTimeout(() => {
      pulsefxApi.saveSettings({ enabled, preamp, headphoneEq, effectEnabled, effectAmounts, eq, preset, outputId }).catch(() => {});
    }, 250);
    return () => clearTimeout(timer);
  }, [hydrated, enabled, preamp, headphoneEq, effectEnabled, effectAmounts, eq, preset, outputId]);

  useEffect(() => {
    if (!hydrated) return undefined;
    const tick = () => {
      refreshStatus();
      if (activeTab === 'apps') refreshApps();
    };
    tick();
    const timer = setInterval(tick, activeTab === 'apps' ? 1200 : 2200);
    return () => clearInterval(timer);
  }, [hydrated, activeTab]);

  const choosePreset = async (name) => {
    const next = [...presets[name]];
    setPreset(name);
    setEq(next);
    await Promise.all(next.map((value, index) => run('eq', index, value)));
  };

  const updateBand = (index, value) => {
    const gain = clamp(value, -12, 12);
    setEq((current) => {
      const next = [...current];
      next[index] = gain;
      return next;
    });
    setPreset('Custom');
    run('eq', index, gain);
  };

  const updateEffectAmount = (value) => {
    const amount = clamp(value, 0, 100);
    setEffectAmounts((current) => ({ ...current, [activeEffect]: amount }));
    if (effectEnabled[activeEffect]) sendEffect(activeEffect, true, amount);
  };

  const toggleEffect = (id) => {
    setActiveEffect(id);
    setEffectEnabled((current) => {
      const turningOn = !current[id];
      const next = { ...current, [id]: turningOn };
      const changed = new Set([id]);
      if (turningOn) {
        for (const blocked of incompatible[id] ?? []) {
          if (next[blocked]) changed.add(blocked);
          next[blocked] = false;
        }
      }
      for (const effectId of changed) sendEffect(effectId, Boolean(next[effectId]), effectAmounts[effectId] ?? 0);
      return next;
    });
  };

  const toggleMaster = () => {
    const next = !enabled;
    setEnabled(next);
    run('enabled', next);
  };

  const changePreamp = (value) => {
    const next = clamp(value, -12, 9);
    setPreamp(next);
    run('preamp', next);
  };

  const toggleHeadphoneEq = () => {
    const next = !headphoneEq;
    setHeadphoneEq(next);
    run('headphone_enable', next);
  };

  const changeOutput = async (deviceId) => {
    setOutputId(deviceId);
    const next = await run('output', deviceId || 'auto');
    if (next?.type === 'status') setStatus(next);
  };

  const changeAppVolume = (pid, value) => {
    const volume = clamp(value, 0, 1);
    setApps((current) => current.map((app) => app.pid === pid ? { ...app, volume } : app));
    run('app_volume', pid, volume);
  };

  const toggleAppMute = (pid, muted) => {
    setApps((current) => current.map((app) => app.pid === pid ? { ...app, muted } : app));
    run('app_mute', pid, muted);
  };

  const healthError = hostError || status.error;
  const stats = status.stats ?? {};
  const hasDrops = (stats.underruns ?? 0) > 0 || (stats.overruns ?? 0) > 0;

  return <main className="app-shell">
    <header className="titlebar">
      <div className="brand"><div className="brand-mark"><AudioLines size={18}/></div><div><strong>PulseFX</strong><span>system audio</span></div></div>
      <div className={healthError || hasDrops ? 'health warning' : status.running ? 'health online' : 'health'}><span className="health-dot"/><span>{healthError ? 'Audio issue' : hasDrops ? 'Dropout detected' : status.running ? 'Engine live' : 'Engine offline'}</span></div>
      <label className="output-pill">
        <Headphones size={15}/><span><small>OUTPUT</small>{physicalDevices.find((device) => device.id === outputId)?.name || 'Choose physical output'}</span><ChevronDown size={14}/>
        <select aria-label="Physical audio output" value={outputId} onChange={(event) => changeOutput(event.target.value)}><option value="">Auto physical device</option>{physicalDevices.map((device) => <option key={device.id} value={device.id}>{device.name}</option>)}</select>
      </label>
      <button className={enabled ? 'master on' : 'master'} onClick={toggleMaster}><Power size={17}/><span>{enabled ? 'On' : 'Off'}</span></button>
    </header>

    {healthError && <div className="system-banner"><CircleAlert size={15}/><span>{healthError}</span></div>}

    <section className={enabled ? 'workspace' : 'workspace disabled'}>
      <aside className="sidebar">
        <button className={activeTab === 'enhance' ? 'nav active' : 'nav'} onClick={() => setActiveTab('enhance')}><Sparkles size={18}/><span>Enhance</span></button>
        <button className={activeTab === 'equalizer' ? 'nav active' : 'nav'} onClick={() => setActiveTab('equalizer')}><SlidersHorizontal size={18}/><span>Equalizer</span></button>
        <button className={activeTab === 'headphones' ? 'nav active' : 'nav'} onClick={() => setActiveTab('headphones')}><Headphones size={18}/><span>Headphones</span></button>
        <button className={activeTab === 'apps' ? 'nav active' : 'nav'} onClick={() => { setActiveTab('apps'); refreshApps(); }}><Radio size={18}/><span>Apps</span></button>
      </aside>

      <div className="content">
        {activeTab === 'enhance' && <section className="enhancer-card">
          <div className="effects-strip">{effects.map((effect) => { const Icon = effect.icon; const on = Boolean(effectEnabled[effect.id]); return <button key={effect.id} onClick={() => toggleEffect(effect.id)} className={activeEffect === effect.id ? 'effect-chip selected' : 'effect-chip'}><span className={on ? 'effect-led on' : 'effect-led'}/><Icon size={17}/><span>{effect.label}</span></button>; })}</div>
          <div className="focus-zone">
            <div className="focus-copy"><p className="kicker">{effectEnabled[activeEffect] ? 'ACTIVE EFFECT' : 'EFFECT OFF'}</p><h1>{active.label}</h1><p>{active.description}</p></div>
            <div className="dial-wrap"><div className="dial-halo" style={{ '--intensity': `${intensity * 3.6}deg` }}><div className="dial"><ActiveIcon size={34}/><strong>{intensity}%</strong><span>Intensity</span></div></div><input aria-label={`${active.label} intensity`} className="dial-range" type="range" min="0" max="100" value={intensity} onChange={(event) => updateEffectAmount(event.target.value)}/></div>
            <div className="quick-controls"><label><span><small>PREAMP</small><strong>{preamp > 0 ? '+' : ''}{preamp.toFixed(1)} dB</strong></span><input type="range" min="-12" max="9" step="0.5" value={preamp} onChange={(event) => changePreamp(event.target.value)}/></label><button className={headphoneEq ? 'headphone-toggle on' : 'headphone-toggle'} onClick={toggleHeadphoneEq}><Headphones size={17}/><span><small>HEADPHONE EQ</small><strong>{headphoneEq ? 'Enabled' : 'Off'}</strong></span><i/></button></div>
          </div>
        </section>}

        {activeTab === 'equalizer' && <section className="eq-card standalone"><div className="eq-header"><div><p className="kicker">TONE SHAPING</p><h2>31-band equalizer</h2></div><div className="preset-row">{Object.keys(presets).map((name) => <button key={name} className={preset === name ? 'preset active' : 'preset'} onClick={() => choosePreset(name)}>{name}</button>)}{preset === 'Custom' && <button className="preset active">Custom</button>}</div></div><div className="eq-viewport"><div className="curve" aria-hidden="true"><svg viewBox="0 0 930 110" preserveAspectRatio="none"><polyline points={curve.map((y, index) => `${index * 31},${y}`).join(' ')}/></svg></div><div className="eq-grid">{bands.map((band, index) => <label className="eq-band" key={band}><span className="db">{eq[index] > 0 ? '+' : ''}{eq[index]}</span><input aria-label={`${band} Hz`} type="range" min="-12" max="12" step="1" value={eq[index]} onChange={(event) => updateBand(index, event.target.value)}/><span className="frequency">{band}</span></label>)}</div></div></section>}

        {activeTab === 'headphones' && <section className="utility-card"><p className="kicker">HEADPHONE CORRECTION</p><div className="utility-heading"><div><h1>Headphone EQ</h1><p>Apply the active correction profile before enhancement and spatial processing.</p></div><button className={headphoneEq ? 'large-toggle on' : 'large-toggle'} onClick={toggleHeadphoneEq}><Power size={18}/>{headphoneEq ? 'Enabled' : 'Disabled'}</button></div><div className="info-grid"><div><small>PROCESSING ORDER</small><strong>Correction → EQ → Enhancement</strong></div><div><small>ENGINE</small><strong>Parametric profile correction</strong></div><div><small>STATUS</small><strong>{headphoneEq ? 'Correction active' : 'Transparent'}</strong></div></div><p className="muted-note">Invalid or missing correction data fails open instead of muting system audio.</p></section>}

        {activeTab === 'apps' && <section className="utility-card apps-card"><div className="utility-heading"><div><p className="kicker">APP VOLUME CONTROLLER</p><h1>Applications</h1><p>Control sessions currently playing through PulseFX Output.</p></div><button className="refresh-button" onClick={refreshApps}>Refresh</button></div><div className="app-list">{apps.length === 0 && <div className="empty-state">No active audio sessions are currently routed through PulseFX.</div>}{apps.map((app) => <div className="app-row" key={app.pid}><div className="app-identity"><div className="app-icon"><AudioLines size={16}/></div><div><strong>{app.name}</strong><span>PID {app.pid}</span></div></div><input aria-label={`${app.name} volume`} type="range" min="0" max="1" step="0.01" value={app.volume} onChange={(event) => changeAppVolume(app.pid, event.target.value)}/><span className="app-volume">{Math.round(app.volume * 100)}%</span><button className={app.muted ? 'mute-button muted' : 'mute-button'} onClick={() => toggleAppMute(app.pid, !app.muted)}>{app.muted ? <VolumeX size={17}/> : <Volume2 size={17}/>}</button></div>)}</div></section>}

        <footer className="engine-footer"><span>Underruns <strong>{stats.underruns ?? 0}</strong></span><span>Overruns <strong>{stats.overruns ?? 0}</strong></span><span>Buffer <strong>{stats.bufferedFrames ?? 0} fr</strong></span><span>Clock <strong>{Number(stats.clockCorrectionPpm ?? 0).toFixed(0)} ppm</strong></span></footer>
      </div>
    </section>
  </main>;
}

createRoot(document.getElementById('root')).render(<React.StrictMode><App/></React.StrictMode>);
