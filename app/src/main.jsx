import React, { useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { AudioLines, ChevronDown, Headphones, Layers3, Moon, Power, Radio, SlidersHorizontal, Sparkles, Waves } from 'lucide-react';
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
  { id: 'ambience', label: 'Ambience', icon: Radio, description: 'Short natural early reflections without a long artificial reverb tail.' },
  { id: 'night', label: 'Night Mode', icon: Moon, description: 'Tighter dynamics for clearer quiet detail and less aggressive peaks.' },
];

const incompatible = {
  surround: ['spatial', 'ambience', 'night'],
  spatial: ['surround', 'ambience'],
  ambience: ['surround', 'spatial', 'night'],
  night: ['surround', 'ambience'],
  fidelity: [],
};

function App() {
  const [enabled, setEnabled] = useState(true);
  const [activeEffect, setActiveEffect] = useState('fidelity');
  const [effectEnabled, setEffectEnabled] = useState({ fidelity: true, spatial: true });
  const [effectAmounts, setEffectAmounts] = useState({ surround: 48, fidelity: 42, spatial: 34, ambience: 30, night: 55 });
  const [preamp, setPreamp] = useState(0);
  const [preset, setPreset] = useState('Music');
  const [eq, setEq] = useState([...presets.Music]);
  const [headphoneEq, setHeadphoneEq] = useState(false);
  const active = effects.find((effect) => effect.id === activeEffect) ?? effects[0];
  const ActiveIcon = active.icon;
  const intensity = effectAmounts[activeEffect] ?? 0;
  const curve = useMemo(() => eq.map((value) => 50 - value * 2.7), [eq]);

  const choosePreset = (name) => { setPreset(name); setEq([...presets[name]]); };
  const updateBand = (index, value) => { const next = [...eq]; next[index] = Number(value); setEq(next); setPreset('Custom'); };
  const updateEffectAmount = (value) => setEffectAmounts((current) => ({ ...current, [activeEffect]: Number(value) }));
  const toggleEffect = (id) => {
    setActiveEffect(id);
    setEffectEnabled((current) => {
      const turningOn = !current[id];
      const next = { ...current, [id]: turningOn };
      if (turningOn) {
        for (const blocked of incompatible[id] ?? []) next[blocked] = false;
      }
      return next;
    });
  };

  return <main className="app-shell">
    <header className="titlebar">
      <div className="brand"><div className="brand-mark"><AudioLines size={18}/></div><div><strong>PulseFX</strong><span>system audio</span></div></div>
      <button className="output-pill"><Headphones size={15}/><span><small>OUTPUT</small>Default audio device</span><ChevronDown size={14}/></button>
      <button className={enabled ? 'master on' : 'master'} onClick={() => setEnabled(!enabled)}><Power size={17}/><span>{enabled ? 'On' : 'Off'}</span></button>
    </header>

    <section className={enabled ? 'workspace' : 'workspace disabled'}>
      <aside className="sidebar">
        <button className="nav active"><Sparkles size={18}/><span>Enhance</span></button>
        <button className="nav"><SlidersHorizontal size={18}/><span>Equalizer</span></button>
        <button className="nav"><Headphones size={18}/><span>Headphones</span></button>
        <button className="nav"><Radio size={18}/><span>Apps</span></button>
      </aside>

      <div className="content">
        <section className="enhancer-card">
          <div className="effects-strip">
            {effects.map((effect) => {
              const Icon = effect.icon;
              const on = Boolean(effectEnabled[effect.id]);
              return <button key={effect.id} onClick={() => toggleEffect(effect.id)} className={activeEffect === effect.id ? 'effect-chip selected' : 'effect-chip'}>
                <span className={on ? 'effect-led on' : 'effect-led'} />
                <Icon size={17}/><span>{effect.label}</span>
              </button>;
            })}
          </div>

          <div className="focus-zone">
            <div className="focus-copy">
              <p className="kicker">{effectEnabled[activeEffect] ? 'ACTIVE EFFECT' : 'EFFECT OFF'}</p>
              <h1>{active.label}</h1>
              <p>{active.description}</p>
            </div>
            <div className="dial-wrap">
              <div className="dial-halo" style={{ '--intensity': `${intensity * 3.6}deg` }}><div className="dial"><ActiveIcon size={34}/><strong>{intensity}%</strong><span>Intensity</span></div></div>
              <input aria-label={`${active.label} intensity`} className="dial-range" type="range" min="0" max="100" value={intensity} onChange={(event) => updateEffectAmount(event.target.value)}/>
            </div>
            <div className="quick-controls">
              <label><span><small>PREAMP</small><strong>{preamp > 0 ? '+' : ''}{preamp.toFixed(1)} dB</strong></span><input type="range" min="-12" max="9" step="0.5" value={preamp} onChange={(event) => setPreamp(Number(event.target.value))}/></label>
              <button className={headphoneEq ? 'headphone-toggle on' : 'headphone-toggle'} onClick={() => setHeadphoneEq(!headphoneEq)}><Headphones size={17}/><span><small>HEADPHONE EQ</small><strong>{headphoneEq ? 'Enabled' : 'Off'}</strong></span><i /></button>
            </div>
          </div>
        </section>

        <section className="eq-card">
          <div className="eq-header"><div><p className="kicker">TONE SHAPING</p><h2>31-band equalizer</h2></div><div className="preset-row">{Object.keys(presets).map((name) => <button key={name} className={preset === name ? 'preset active' : 'preset'} onClick={() => choosePreset(name)}>{name}</button>)}{preset === 'Custom' && <button className="preset active">Custom</button>}</div></div>
          <div className="eq-viewport"><div className="curve" aria-hidden="true"><svg viewBox="0 0 930 110" preserveAspectRatio="none"><polyline points={curve.map((y, index) => `${index * 31},${y}`).join(' ')} /></svg></div><div className="eq-grid">{bands.map((band, index) => <label className="eq-band" key={band}><span className="db">{eq[index] > 0 ? '+' : ''}{eq[index]}</span><input aria-label={`${band} Hz`} type="range" min="-12" max="12" step="1" value={eq[index]} onChange={(event) => updateBand(index, event.target.value)}/><span className="frequency">{band}</span></label>)}</div></div>
        </section>
      </div>
    </section>
  </main>;
}

createRoot(document.getElementById('root')).render(<React.StrictMode><App/></React.StrictMode>);
