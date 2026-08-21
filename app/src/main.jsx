import React, { useMemo, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Headphones, Power, Sparkles, Waves, Moon, SlidersHorizontal } from 'lucide-react';
import './styles.css';

const bands = ['31','62','125','250','500','1k','2k','4k','8k','16k'];
const presets = {
  Flat: [0,0,0,0,0,0,0,0,0,0],
  Music: [3,2,1,0,-1,0,1,2,3,2],
  Movies: [2,2,1,0,1,2,3,2,1,0],
  Vocal: [-2,-1,0,1,2,3,3,2,0,-1],
  Night: [-2,-2,-1,0,1,2,2,1,0,-1],
};

function App(){
  const [enabled,setEnabled]=useState(true);
  const [preset,setPreset]=useState('Music');
  const [eq,setEq]=useState(presets.Music);
  const [width,setWidth]=useState(32);
  const [clarity,setClarity]=useState(22);
  const [bass,setBass]=useState(28);
  const [night,setNight]=useState(false);

  const curve = useMemo(() => eq.map(v => `${50 + v*3}%`).join(' '), [eq]);
  const choosePreset = (name) => { setPreset(name); setEq([...presets[name]]); };
  const updateBand=(i,value)=>{const next=[...eq];next[i]=Number(value);setEq(next);setPreset('Custom');};

  return <main className="app-shell">
    <header className="topbar">
      <div className="brand"><div className="brand-mark"><Waves size={18}/></div><span>PulseFX</span></div>
      <div className="device"><Headphones size={16}/><span>System Output</span><strong>Default device</strong></div>
      <button className={enabled?'power on':'power'} onClick={()=>setEnabled(!enabled)} aria-label="Toggle PulseFX"><Power size={18}/></button>
    </header>

    <section className={!enabled?'workspace disabled':'workspace'}>
      <aside className="rail">
        <button className="rail-item active"><Sparkles size={18}/><span>Enhance</span></button>
        <button className="rail-item"><SlidersHorizontal size={18}/><span>Equalizer</span></button>
        <button className="rail-item"><Headphones size={18}/><span>Headphones</span></button>
      </aside>

      <div className="content">
        <div className="hero">
          <div>
            <p className="eyebrow">SYSTEM-WIDE AUDIO</p>
            <h1>Hear everything<br/><em>open up.</em></h1>
            <p className="lede">Clean loudness, precise tone and spacious imaging — tuned as one signal chain.</p>
          </div>
          <div className="orb-wrap">
            <div className={enabled?'orb live':'orb'}><div className="orb-core"><Waves size={42}/></div></div>
            <span>{enabled?'Processing active':'Bypassed'}</span>
          </div>
        </div>

        <div className="presets">
          {Object.keys(presets).map(name=><button key={name} className={preset===name?'preset active':'preset'} onClick={()=>choosePreset(name)}>{name}</button>)}
          {preset==='Custom' && <button className="preset active">Custom</button>}
        </div>

        <section className="panel eq-panel">
          <div className="panel-heading"><div><p className="eyebrow">TONE</p><h2>Equalizer</h2></div><span className="status">10-band</span></div>
          <div className="eq-grid">
            {bands.map((band,i)=><label className="eq-band" key={band}>
              <span className="db">{eq[i]>0?'+':''}{eq[i]} dB</span>
              <input aria-label={`${band} Hz`} type="range" min="-12" max="12" step="1" value={eq[i]} onChange={e=>updateBand(i,e.target.value)}/>
              <span>{band}</span>
            </label>)}
          </div>
          <div className="curve-note">Current curve <code>{curve}</code></div>
        </section>

        <section className="effects-grid">
          <Effect title="Space" subtitle="Stereo image" value={width} setValue={setWidth} icon={<Waves size={18}/>}/>
          <Effect title="Clarity" subtitle="Detail presence" value={clarity} setValue={setClarity} icon={<Sparkles size={18}/>}/>
          <Effect title="Bass" subtitle="Low-end weight" value={bass} setValue={setBass} icon={<Headphones size={18}/>}/>
          <article className={night?'effect-card active-card':'effect-card'}>
            <div className="effect-top"><div className="effect-icon"><Moon size={18}/></div><div><h3>Night mode</h3><p>Dynamic control</p></div></div>
            <button className={night?'toggle on':'toggle'} onClick={()=>setNight(!night)}><span/></button>
          </article>
        </section>
      </div>
    </section>
  </main>
}

function Effect({title,subtitle,value,setValue,icon}){
  return <article className="effect-card">
    <div className="effect-top"><div className="effect-icon">{icon}</div><div><h3>{title}</h3><p>{subtitle}</p></div><strong>{value}%</strong></div>
    <input className="horizontal" type="range" min="0" max="100" value={value} onChange={e=>setValue(Number(e.target.value))}/>
  </article>
}

createRoot(document.getElementById('root')).render(<React.StrictMode><App/></React.StrictMode>);
