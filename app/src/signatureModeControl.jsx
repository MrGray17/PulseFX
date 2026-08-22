import React, { useEffect, useState } from 'react';
import { createRoot } from 'react-dom/client';
import { Sparkles, SlidersHorizontal } from 'lucide-react';
import { pulsefxApi } from './pulsefxApi.js';
import './signatureModeControl.css';

function SignatureModeControl() {
  const [mode, setMode] = useState(() => pulsefxApi.getProcessingMode());
  const [changing, setChanging] = useState(false);

  useEffect(() => pulsefxApi.onProcessingMode(setMode), []);

  const choose = async (nextMode) => {
    if (changing || nextMode === mode) return;
    setChanging(true);
    try {
      await pulsefxApi.setProcessingMode(nextMode);
    } finally {
      setChanging(false);
    }
  };

  return <div className="processing-mode-control" role="group" aria-label="PulseFX processing mode">
    <button
      type="button"
      aria-pressed={mode === 'signature'}
      className={mode === 'signature' ? 'processing-mode-option active' : 'processing-mode-option'}
      onClick={() => choose('signature')}
      disabled={changing}
      title="Adaptive enhancement based on device response, output volume, and headroom policy"
    >
      <Sparkles size={13}/><span><strong>Signature</strong><small>Adaptive</small></span>
    </button>
    <button
      type="button"
      aria-pressed={mode === 'manual'}
      className={mode === 'manual' ? 'processing-mode-option active' : 'processing-mode-option'}
      onClick={() => choose('manual')}
      disabled={changing}
      title="Use the EQ and enhancement controls exactly as configured"
    >
      <SlidersHorizontal size={13}/><span><strong>Manual</strong><small>Your controls</small></span>
    </button>
  </div>;
}

const mount = document.getElementById('signature-mode-root');
if (mount) createRoot(mount).render(<SignatureModeControl/>);
