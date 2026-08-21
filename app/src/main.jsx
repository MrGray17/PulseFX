import React, { useEffect, useMemo, useRef, useState } from 'react';
import { createRoot } from 'react-dom/client';
import {
  AudioLines, ChevronDown, CircleAlert, Globe2, Headphones, Layers3, Moon,
  Music2, Pause, Play, Plus, Power, Radio, Search, Settings2, SkipBack,
  SkipForward, SlidersHorizontal, Sparkles, Trash2, Volume2, VolumeX, Waves,
} from 'lucide-react';
import { pulsefxApi } from './pulsefxApi.js';
import './styles.css';

const bands = ['20','25','31','40','50','63','80','100','125','160','200','250','315','400','500','630','800','1k','1.25k','1.6k','2k','2.5k','3.15k','4k','5k','6.3k','8k','10k','12.5k','16k','20k'];
const flat = () => bands.map(() => 0);
const presets = {
  Flat: flat(),
  Pop: [-1,-1,0,1,2,2,2,1,0,-1,-1,0,1,2,3,3,2,2,1,1,2,2,3,3,3,2,1,1,1,0,0],
  Loud: [4,4,4,4,4,3,3,2,1,0,-1,-1,-1,0,1,2,2,2,2,2,2,2,3,3,4,4,4,4,3,2,2],
  Classical: [0,0,0,0,0,0,-1,-1,-1,-1,-1,0,0,1,2,2,2,2,1,1,1,2,2,2,2,1,1,0,0,0,0],
  Party: [4,4,4,4,3,3,2,1,0,-1,-1,-1,0,1,2,2,2,1,1,2,3,3,4,4,4,4,3,2,2,1,1],
  Reggae: [1,2,3,4,4,3,2,1,0,-1,-2,-2,-1,0,1,2,2,1,0,0,1,2,2,2,1,1,0,0,0,0,0],
  Movie: [3,3,3,3,2,2,1,0,0,0,1,2,3,4,4,4,3,3,2,2,2,2,2,2,2,1,1,1,1,0,0],
  'Hip-hop': [4,4,5,5,5,4,3,2,1,0,-1,-1,0,1,2,3,3,2,2,2,2,3,3,3,2,2,1,1,0,0,0],
  Jazz: [2,2,2,1,1,0,-1,-1,0,1,2,3,3,3,2,1,1,1,2,2,3,3,3,2,2,2,2,2,1,1,1],
  Deep: [5,5,5,5,5,4,3,2,1,0,-1,-2,-2,-1,0,1,1,1,1,1,1,1,1,1,0,0,0,0,-1,-1,-1],
  Dubstep: [5,5,5,5,5,4,3,2,0,-1,-2,-2,-1,0,1,2,2,2,1,2,3,4,5,5,5,4,3,2,1,1,1],
  Trap: [5,5,5,5,4,4,3,2,1,0,-1,-2,-2,-1,0,1,2,2,2,2,3,4,5,5,4,3,2,1,1,0,0],
};

const effects = [
  { id: 'surround', label: '3D Surround', icon: Layers3, description: 'Binaural HRTF rendering with multichannel speaker virtualization.' },
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
const defaultShortcuts = {
  toggleProcessing: 'Control+Alt+B',
  toggleSurround: 'Control+Alt+3',
  showEqualizer: 'Control+Alt+E',
  showPlayer: 'Control+Alt+P',
};
const shortcutLabels = {
  toggleProcessing: 'Toggle processing',
  toggleSurround: 'Toggle 3D Surround',
  showEqualizer: 'Open Equalizer',
  showPlayer: 'Open Audio Player',
};

function clamp(value, min, max) {
  const number = Number(value);
  return Number.isFinite(number) ? Math.min(max, Math.max(min, number)) : min;
}

function formatTime(seconds) {
  if (!Number.isFinite(seconds) || seconds < 0) return '0:00';
  const whole = Math.floor(seconds);
  return `${Math.floor(whole / 60)}:${String(whole % 60).padStart(2, '0')}`;
}

function normalizePlaylists(value) {
  if (!Array.isArray(value)) return [{ id: 'library', name: 'Library', tracks: [] }];
  const result = value.slice(0, 50).map((playlist) => ({
    id: typeof playlist?.id === 'string' && playlist.id.length <= 100 ? playlist.id : crypto.randomUUID(),
    name: typeof playlist?.name === 'string' && playlist.name.trim() ? playlist.name.trim().slice(0, 80) : 'Playlist',
    tracks: Array.isArray(playlist?.tracks) ? playlist.tracks.slice(0, 2000).filter((track) =>
      typeof track?.id === 'string' && typeof track?.name === 'string' &&
      typeof track?.fileUrl === 'string' && track.fileUrl.startsWith('file:')) : [],
  }));
  return result.length ? result : [{ id: 'library', name: 'Library', tracks: [] }];
}

function acceleratorFromEvent(event) {
  event.preventDefault();
  const ignored = new Set(['Control', 'Shift', 'Alt', 'Meta']);
  if (ignored.has(event.key)) return '';
  const parts = [];
  if (event.ctrlKey) parts.push('Control');
  if (event.altKey) parts.push('Alt');
  if (event.shiftKey) parts.push('Shift');
  if (event.metaKey) parts.push('Super');
  const special = { ' ': 'Space', ArrowUp: 'Up', ArrowDown: 'Down', ArrowLeft: 'Left', ArrowRight: 'Right', Escape: 'Esc' };
  const key = special[event.key] || (event.key.length === 1 ? event.key.toUpperCase() : event.key);
  if (!key || key === 'Unidentified') return '';
  parts.push(key);
  return parts.join('+');
}

function App() {
  const audioRef = useRef(null);
  const [enabled, setEnabled] = useState(true);
  const [activeTab, setActiveTab] = useState('enhance');
  const [activeEffect, setActiveEffect] = useState('fidelity');
  const [effectEnabled, setEffectEnabled] = useState(defaultEffectEnabled);
  const [effectAmounts, setEffectAmounts] = useState(defaultEffectAmounts);
  const [preamp, setPreamp] = useState(0);
  const [pitch, setPitch] = useState(0);
  const [preset, setPreset] = useState('Pop');
  const [eq, setEq] = useState([...presets.Pop]);
  const [headphoneEq, setHeadphoneEq] = useState(false);
  const [headphoneModels, setHeadphoneModels] = useState([]);
  const [headphoneQuery, setHeadphoneQuery] = useState('');
  const [headphoneRevision, setHeadphoneRevision] = useState('');
  const [selectedHeadphone, setSelectedHeadphone] = useState(null);
  const [headphoneProfileInfo, setHeadphoneProfileInfo] = useState(null);
  const [headphoneLoading, setHeadphoneLoading] = useState(false);
  const [headphoneError, setHeadphoneError] = useState('');
  const [outputId, setOutputId] = useState('');
  const [devices, setDevices] = useState([]);
  const [apps, setApps] = useState([]);
  const [status, setStatus] = useState({ running: false, error: '', stats: {} });
  const [hostError, setHostError] = useState('');
  const [hydrated, setHydrated] = useState(false);
  const [shortcuts, setShortcuts] = useState(defaultShortcuts);

  const [playlists, setPlaylists] = useState([{ id: 'library', name: 'Library', tracks: [] }]);
  const [activePlaylistId, setActivePlaylistId] = useState('library');
  const [newPlaylistName, setNewPlaylistName] = useState('');
  const [trackIndex, setTrackIndex] = useState(0);
  const [mediaKind, setMediaKind] = useState('');
  const [isPlaying, setIsPlaying] = useState(false);
  const [mediaTime, setMediaTime] = useState(0);
  const [mediaDuration, setMediaDuration] = useState(0);
  const [mediaVolume, setMediaVolume] = useState(0.8);
  const [activeStation, setActiveStation] = useState(null);
  const [radioStations, setRadioStations] = useState([]);
  const [radioQuery, setRadioQuery] = useState('');
  const [radioLoading, setRadioLoading] = useState(false);
  const [radioError, setRadioError] = useState('');

  const active = effects.find((effect) => effect.id === activeEffect) ?? effects[0];
  const ActiveIcon = active.icon;
  const intensity = effectAmounts[activeEffect] ?? 0;
  const curve = useMemo(() => eq.map((value) => 50 - value * 2.7), [eq]);
  const physicalDevices = devices.filter((device) => !device.pulsefx);
  const headphoneMatches = useMemo(() => {
    const query = headphoneQuery.trim().toLocaleLowerCase();
    const source = query.length === 0 ? headphoneModels : headphoneModels.filter((model) => model.name.toLocaleLowerCase().includes(query));
    return source.slice(0, 80);
  }, [headphoneModels, headphoneQuery]);
  const activePlaylist = playlists.find((item) => item.id === activePlaylistId) ?? playlists[0];
  const activeTrack = activePlaylist?.tracks?.[trackIndex] ?? null;
  const mediaSource = mediaKind === 'radio' ? activeStation?.streamUrl ?? '' : mediaKind === 'local' ? activeTrack?.fileUrl ?? '' : '';
  const mediaTitle = mediaKind === 'radio' ? activeStation?.name ?? '' : activeTrack?.name ?? '';
  const mediaSubtitle = mediaKind === 'radio'
    ? [activeStation?.countrycode, activeStation?.codec, activeStation?.bitrate ? `${activeStation.bitrate} kbps` : ''].filter(Boolean).join(' · ')
    : activePlaylist?.name ?? '';

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

  const loadHeadphoneCatalog = async () => {
    if (headphoneModels.length > 0 || headphoneLoading) return;
    setHeadphoneLoading(true);
    setHeadphoneError('');
    try {
      const result = await pulsefxApi.listHeadphones();
      setHeadphoneModels(Array.isArray(result?.models) ? result.models : []);
      setHeadphoneRevision(typeof result?.revision === 'string' ? result.revision : '');
    } catch (error) {
      setHeadphoneError(error instanceof Error ? error.message : String(error));
    } finally { setHeadphoneLoading(false); }
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
    await run('pitch', state.pitch);
    await run('headphone_enable', false);
    for (const effect of effects) await sendEffect(effect.id, Boolean(state.effectEnabled[effect.id]), state.effectAmounts[effect.id] ?? 0);
    for (let index = 0; index < state.eq.length; index += 1) await run('eq', index, state.eq[index]);
    if (state.outputId) await run('output', state.outputId);
    if (state.headphoneModelPath) {
      try {
        const applied = await pulsefxApi.applyHeadphoneProfile(state.headphoneModelPath);
        if (applied?.ok) {
          setSelectedHeadphone(applied.model ?? { path: state.headphoneModelPath, name: state.headphoneModelName || 'Selected model' });
          setHeadphoneRevision(applied.revision ?? state.headphoneRevision ?? '');
          setHeadphoneProfileInfo({ preampDb: applied.preampDb, filters: applied.filters });
          if (state.headphoneEq) await run('headphone_enable', true);
          return;
        }
      } catch (error) { setHeadphoneError(error instanceof Error ? error.message : String(error)); }
    }
    if (state.headphoneEq && !state.headphoneModelPath) setHeadphoneError('Choose a headphone model before enabling correction.');
  };

  useEffect(() => {
    let cancelled = false;
    (async () => {
      const saved = await pulsefxApi.loadSettings();
      if (cancelled) return;
      const restoredPreset = typeof saved.preset === 'string' && presets[saved.preset] ? saved.preset : 'Pop';
      const restoredPlaylists = normalizePlaylists(saved.playlists);
      const restoredPlaylistId = typeof saved.activePlaylistId === 'string' && restoredPlaylists.some((item) => item.id === saved.activePlaylistId) ? saved.activePlaylistId : restoredPlaylists[0].id;
      const restored = {
        enabled: typeof saved.enabled === 'boolean' ? saved.enabled : true,
        preamp: clamp(saved.preamp ?? 0, -12, 9),
        pitch: clamp(saved.pitch ?? 0, -5, 5),
        headphoneEq: typeof saved.headphoneEq === 'boolean' ? saved.headphoneEq : false,
        headphoneModelPath: typeof saved.headphoneModelPath === 'string' ? saved.headphoneModelPath : '',
        headphoneModelName: typeof saved.headphoneModelName === 'string' ? saved.headphoneModelName : '',
        headphoneRevision: typeof saved.headphoneRevision === 'string' ? saved.headphoneRevision : '',
        effectEnabled: { ...defaultEffectEnabled, ...(saved.effectEnabled ?? {}) },
        effectAmounts: { ...defaultEffectAmounts, ...(saved.effectAmounts ?? {}) },
        eq: Array.isArray(saved.eq) && saved.eq.length === bands.length ? saved.eq.map((value) => clamp(value, -12, 12)) : [...presets[restoredPreset]],
        preset: restoredPreset,
        outputId: typeof saved.outputId === 'string' ? saved.outputId : '',
      };
      setEnabled(restored.enabled); setPreamp(restored.preamp); setPitch(restored.pitch); setHeadphoneEq(restored.headphoneEq);
      setHeadphoneRevision(restored.headphoneRevision);
      if (restored.headphoneModelPath) setSelectedHeadphone({ path: restored.headphoneModelPath, name: restored.headphoneModelName || 'Selected model' });
      setEffectEnabled(restored.effectEnabled); setEffectAmounts(restored.effectAmounts); setEq(restored.eq); setPreset(restored.preset); setOutputId(restored.outputId);
      setPlaylists(restoredPlaylists); setActivePlaylistId(restoredPlaylistId);
      setMediaVolume(clamp(saved.mediaVolume ?? 0.8, 0, 1));
      setShortcuts({ ...defaultShortcuts, ...(saved.shortcuts ?? {}) });
      setHydrated(true);
      await syncAllControls(restored);
      await Promise.all([refreshStatus(), refreshDevices()]);
    })();
    return () => { cancelled = true; };
  }, []);

  useEffect(() => {
    const removeEvent = pulsefxApi.onEvent((message) => { if (message?.type === 'status') setStatus(message); });
    const removeHostState = pulsefxApi.onHostState((message) => {
      if (message?.running === false) setHostError(message.error || 'PulseFX audio host stopped');
      if (message?.running === true) setHostError('');
    });
    return () => { removeEvent(); removeHostState(); };
  }, []);

  useEffect(() => {
    if (!hydrated) return undefined;
    const timer = setTimeout(() => {
      pulsefxApi.saveSettings({
        enabled, preamp, pitch, headphoneEq,
        headphoneModelPath: selectedHeadphone?.path ?? '', headphoneModelName: selectedHeadphone?.name ?? '', headphoneRevision,
        effectEnabled, effectAmounts, eq, preset, outputId, playlists, activePlaylistId, mediaVolume, shortcuts,
      }).catch(() => {});
    }, 250);
    return () => clearTimeout(timer);
  }, [hydrated, enabled, preamp, pitch, headphoneEq, selectedHeadphone, headphoneRevision, effectEnabled, effectAmounts, eq, preset, outputId, playlists, activePlaylistId, mediaVolume, shortcuts]);

  useEffect(() => {
    if (!hydrated) return undefined;
    const tick = () => { refreshStatus(); if (activeTab === 'apps') refreshApps(); };
    tick();
    const timer = setInterval(tick, activeTab === 'apps' ? 1200 : 2200);
    return () => clearInterval(timer);
  }, [hydrated, activeTab]);

  useEffect(() => {
    if (audioRef.current) audioRef.current.volume = mediaVolume;
  }, [mediaVolume]);

  useEffect(() => {
    if (!mediaSource || !audioRef.current) return;
    setMediaTime(0); setMediaDuration(0);
    audioRef.current.load();
    audioRef.current.play().then(() => setIsPlaying(true)).catch(() => setIsPlaying(false));
  }, [mediaSource]);

  const choosePreset = async (name) => {
    if (!presets[name]) return;
    const next = [...presets[name]];
    setPreset(name); setEq(next);
    await Promise.all(next.map((value, index) => run('eq', index, value)));
  };

  const updateBand = (index, value) => {
    const gain = clamp(value, -12, 12);
    setEq((current) => { const next = [...current]; next[index] = gain; return next; });
    setPreset('Custom'); run('eq', index, gain);
  };
  const updateEffectAmount = (value) => {
    const amount = clamp(value, 0, 100);
    setEffectAmounts((current) => ({ ...current, [activeEffect]: amount }));
    if (effectEnabled[activeEffect]) sendEffect(activeEffect, true, amount);
  };

  const setEffectTo = (id, turningOn) => {
    setActiveEffect(id);
    setEffectEnabled((current) => {
      if (Boolean(current[id]) === turningOn) return current;
      const next = { ...current, [id]: turningOn };
      const changed = new Set([id]);
      if (turningOn) {
        for (const blocked of incompatible[id] ?? []) { if (next[blocked]) changed.add(blocked); next[blocked] = false; }
      }
      for (const effectId of changed) sendEffect(effectId, Boolean(next[effectId]), effectAmounts[effectId] ?? 0);
      return next;
    });
  };
  const toggleEffect = (id) => setEffectTo(id, !Boolean(effectEnabled[id]));
  const toggleMaster = () => { const next = !enabled; setEnabled(next); run('enabled', next); };
  const changePreamp = (value) => { const next = clamp(value, -12, 9); setPreamp(next); run('preamp', next); };
  const changePitch = (value) => { const next = clamp(value, -5, 5); setPitch(next); run('pitch', next); };

  const toggleHeadphoneEq = () => {
    if (!selectedHeadphone) { setHeadphoneError('Choose a headphone model before enabling correction.'); setActiveTab('headphones'); loadHeadphoneCatalog(); return; }
    const next = !headphoneEq; setHeadphoneEq(next); setHeadphoneError(''); run('headphone_enable', next);
  };
  const chooseHeadphone = async (model) => {
    if (!model?.path || headphoneLoading) return;
    setHeadphoneLoading(true); setHeadphoneError('');
    try {
      const applied = await pulsefxApi.applyHeadphoneProfile(model.path);
      if (!applied?.ok) throw new Error('Headphone profile was not applied');
      setSelectedHeadphone(applied.model ?? model); setHeadphoneRevision(applied.revision ?? headphoneRevision);
      setHeadphoneProfileInfo({ preampDb: applied.preampDb, filters: applied.filters }); setHeadphoneEq(true);
      await run('headphone_enable', true);
    } catch (error) { setHeadphoneError(error instanceof Error ? error.message : String(error)); }
    finally { setHeadphoneLoading(false); }
  };

  const changeOutput = async (deviceId) => { setOutputId(deviceId); const next = await run('output', deviceId || 'auto'); if (next?.type === 'status') setStatus(next); };
  const changeAppVolume = (pid, value) => { const volume = clamp(value, 0, 1); setApps((current) => current.map((item) => item.pid === pid ? { ...item, volume } : item)); run('app_volume', pid, volume); };
  const toggleAppMute = (pid, muted) => { setApps((current) => current.map((item) => item.pid === pid ? { ...item, muted } : item)); run('app_mute', pid, muted); };

  const addAudioFiles = async () => {
    const files = await pulsefxApi.openAudioFiles();
    if (!Array.isArray(files) || files.length === 0) return;

    const currentTracks = activePlaylist?.tracks ?? [];
    const known = new Set(currentTracks.map((track) => track.id));
    const additions = files.filter((track) => track?.id && !known.has(track.id));
    if (additions.length === 0) return;

    const firstNewIndex = currentTracks.length;
    const activateFirstImport = currentTracks.length === 0;
    setPlaylists((current) => current.map((playlist) => {
      if (playlist.id !== activePlaylistId) return playlist;
      const latestKnown = new Set(playlist.tracks.map((track) => track.id));
      const freshAdditions = additions.filter((track) => !latestKnown.has(track.id));
      return freshAdditions.length > 0 ? { ...playlist, tracks: [...playlist.tracks, ...freshAdditions] } : playlist;
    }));

    if (activateFirstImport) {
      setTrackIndex(firstNewIndex);
      setMediaKind('local');
      setActiveStation(null);
    }
  };

  const createPlaylist = () => {
    const name = newPlaylistName.trim();
    if (!name) return;
    const playlist = { id: crypto.randomUUID(), name: name.slice(0, 80), tracks: [] };
    setPlaylists((current) => [...current, playlist]); setActivePlaylistId(playlist.id); setTrackIndex(0); setNewPlaylistName('');
  };
  const deleteActivePlaylist = () => {
    if (playlists.length <= 1) return;
    const next = playlists.filter((item) => item.id !== activePlaylistId);
    setPlaylists(next); setActivePlaylistId(next[0].id); setTrackIndex(0);
    if (mediaKind === 'local') { setMediaKind(''); setIsPlaying(false); }
  };
  const selectPlaylist = (id) => { setActivePlaylistId(id); setTrackIndex(0); if (mediaKind === 'local') setMediaKind(''); };
  const selectTrack = (index) => { setTrackIndex(index); setMediaKind('local'); setActiveStation(null); };
  const removeTrack = (index) => {
    setPlaylists((current) => current.map((playlist) => playlist.id === activePlaylistId ? { ...playlist, tracks: playlist.tracks.filter((_, track) => track !== index) } : playlist));
    if (mediaKind === 'local' && index === trackIndex) { audioRef.current?.pause(); setMediaKind(''); setIsPlaying(false); }
    if (index < trackIndex) setTrackIndex((current) => Math.max(0, current - 1));
  };
  const previousTrack = () => {
    if (!activePlaylist?.tracks?.length) return;
    setTrackIndex((current) => (current - 1 + activePlaylist.tracks.length) % activePlaylist.tracks.length); setMediaKind('local'); setActiveStation(null);
  };
  const nextTrack = () => {
    if (!activePlaylist?.tracks?.length) return;
    setTrackIndex((current) => (current + 1) % activePlaylist.tracks.length); setMediaKind('local'); setActiveStation(null);
  };
  const togglePlayback = async () => {
    const audio = audioRef.current;
    if (!audio || !mediaSource) return;
    if (audio.paused) { try { await audio.play(); setIsPlaying(true); } catch { setIsPlaying(false); } }
    else { audio.pause(); setIsPlaying(false); }
  };
  const seekMedia = (value) => { const next = clamp(value, 0, mediaDuration || 0); if (audioRef.current) audioRef.current.currentTime = next; setMediaTime(next); };

  const refreshRadio = async (query = radioQuery) => {
    if (radioLoading) return;
    setRadioLoading(true); setRadioError('');
    try { setRadioStations(await pulsefxApi.searchRadio(query.trim())); }
    catch (error) { setRadioError(error instanceof Error ? error.message : String(error)); }
    finally { setRadioLoading(false); }
  };
  const openRadio = () => { setActiveTab('radio'); if (radioStations.length === 0 && !radioLoading) refreshRadio(''); };
  const playStation = (station) => {
    setActiveStation(station); setMediaKind('radio');
    pulsefxApi.recordRadioClick(station.stationuuid).catch(() => {});
  };

  useEffect(() => {
    if (!hydrated) return undefined;
    return pulsefxApi.onQuickAction((message) => {
      if (message?.action === 'tab' && ['enhance','equalizer','headphones','apps','player','radio','settings'].includes(message.tab)) {
        if (message.tab === 'radio') openRadio(); else setActiveTab(message.tab);
      } else if (message?.action === 'preset' && presets[message.preset]) {
        setActiveTab('equalizer'); choosePreset(message.preset);
      } else if (message?.action === 'effect' && message.id === 'surround') {
        setEffectTo('surround', Boolean(message.enabled));
      } else if (message?.action === 'open-files' && Array.isArray(message.files)) {
        const launchFiles = message.files.slice(0, 100).filter((track) =>
          typeof track?.id === 'string' && track.id.length > 0 && track.id.length <= 32767 &&
          typeof track?.name === 'string' && track.name.length > 0 && track.name.length <= 500 &&
          typeof track?.fileUrl === 'string' && track.fileUrl.startsWith('file:'));
        if (launchFiles.length > 0) {
          const launchIds = new Set(launchFiles.map((track) => track.id.toLocaleLowerCase()));
          setPlaylists((current) => {
            const existingLibrary = current.find((playlist) => playlist.id === 'library');
            const library = existingLibrary ?? { id: 'library', name: 'Library', tracks: [] };
            const remaining = library.tracks.filter((track) => !launchIds.has(String(track.id).toLocaleLowerCase()));
            const nextLibrary = { ...library, tracks: [...launchFiles, ...remaining].slice(0, 2000) };
            return existingLibrary
              ? current.map((playlist) => playlist.id === 'library' ? nextLibrary : playlist)
              : [nextLibrary, ...current];
          });
          setActivePlaylistId('library');
          setTrackIndex(0);
          setMediaKind('local');
          setActiveStation(null);
          setActiveTab('player');
        }
      }
    });
  }, [hydrated, effectEnabled, effectAmounts, radioStations.length, radioLoading]);

  const healthError = hostError || status.error;
  const stats = status.stats ?? {};
  const hasDrops = (stats.underruns ?? 0) > 0 || (stats.overruns ?? 0) > 0;

  return <main className="app-shell">
    <audio ref={audioRef} src={mediaSource || undefined} onPlay={() => setIsPlaying(true)} onPause={() => setIsPlaying(false)} onTimeUpdate={(event) => setMediaTime(event.currentTarget.currentTime)} onDurationChange={(event) => setMediaDuration(Number.isFinite(event.currentTarget.duration) ? event.currentTarget.duration : 0)} onEnded={() => { if (mediaKind === 'local') nextTrack(); else setIsPlaying(false); }}/>
    <header className="titlebar">
      <div className="brand"><div className="brand-mark"><AudioLines size={18}/></div><div><strong>PulseFX</strong><span>system audio</span></div></div>
      <div className={healthError || hasDrops ? 'health warning' : status.running ? 'health online' : 'health'}><span className="health-dot"/><span>{healthError ? 'Audio issue' : hasDrops ? 'Dropout detected' : status.running ? 'Engine live' : 'Engine offline'}</span></div>
      <label className="output-pill"><Headphones size={15}/><span><small>OUTPUT</small>{physicalDevices.find((device) => device.id === outputId)?.name || 'Choose physical output'}</span><ChevronDown size={14}/><select aria-label="Physical audio output" value={outputId} onChange={(event) => changeOutput(event.target.value)}><option value="">Auto physical device</option>{physicalDevices.map((device) => <option key={device.id} value={device.id}>{device.name}</option>)}</select></label>
      <button aria-label="Toggle PulseFX processing" className={enabled ? 'master on' : 'master'} onClick={toggleMaster}><Power size={17}/><span>{enabled ? 'On' : 'Off'}</span></button>
    </header>
    {healthError && <div className="system-banner"><CircleAlert size={15}/><span>{healthError}</span></div>}

    <section className={enabled ? 'workspace' : 'workspace disabled'}>
      <aside className="sidebar">
        <button className={activeTab === 'enhance' ? 'nav active' : 'nav'} onClick={() => setActiveTab('enhance')}><Sparkles size={18}/><span>Enhance</span></button>
        <button className={activeTab === 'equalizer' ? 'nav active' : 'nav'} onClick={() => setActiveTab('equalizer')}><SlidersHorizontal size={18}/><span>Equalizer</span></button>
        <button className={activeTab === 'headphones' ? 'nav active' : 'nav'} onClick={() => { setActiveTab('headphones'); loadHeadphoneCatalog(); }}><Headphones size={18}/><span>Headphones</span></button>
        <button className={activeTab === 'apps' ? 'nav active' : 'nav'} onClick={() => { setActiveTab('apps'); refreshApps(); }}><Radio size={18}/><span>Apps</span></button>
        <button className={activeTab === 'player' ? 'nav active' : 'nav'} onClick={() => setActiveTab('player')}><Music2 size={18}/><span>Player</span></button>
        <button className={activeTab === 'radio' ? 'nav active' : 'nav'} onClick={openRadio}><Globe2 size={18}/><span>Radio</span></button>
        <button className={activeTab === 'settings' ? 'nav active nav-bottom' : 'nav nav-bottom'} onClick={() => setActiveTab('settings')}><Settings2 size={18}/><span>Settings</span></button>
      </aside>

      <div className="content">
        {activeTab === 'enhance' && <section className="enhancer-card">
          <div className="effects-strip">{effects.map((effect) => { const Icon = effect.icon; const on = Boolean(effectEnabled[effect.id]); return <button key={effect.id} onClick={() => toggleEffect(effect.id)} className={activeEffect === effect.id ? 'effect-chip selected' : 'effect-chip'}><span className={on ? 'effect-led on' : 'effect-led'}/><Icon size={17}/><span>{effect.label}</span></button>; })}</div>
          <div className="focus-zone"><div className="focus-copy"><p className="kicker">{effectEnabled[activeEffect] ? 'ACTIVE EFFECT' : 'EFFECT OFF'}</p><h1>{active.label}</h1><p>{active.description}</p></div><div className="dial-wrap"><div className="dial-halo" style={{ '--intensity': `${intensity * 3.6}deg` }}><div className="dial"><ActiveIcon size={34}/><strong>{intensity}%</strong><span>Intensity</span></div></div><input aria-label={`${active.label} intensity`} className="dial-range" type="range" min="0" max="100" value={intensity} onChange={(event) => updateEffectAmount(event.target.value)}/></div><div className="quick-controls"><label><span><small>PREAMP</small><strong>{preamp > 0 ? '+' : ''}{preamp.toFixed(1)} dB</strong></span><input aria-label="Preamp" type="range" min="-12" max="9" step="0.5" value={preamp} onChange={(event) => changePreamp(event.target.value)}/></label><label><span><small>PITCH</small><strong>{pitch > 0 ? '+' : ''}{pitch.toFixed(1)} st</strong></span><input aria-label="Pitch semitones" type="range" min="-5" max="5" step="0.1" value={pitch} onChange={(event) => changePitch(event.target.value)}/></label><button aria-label="Toggle headphone correction" className={headphoneEq ? 'headphone-toggle on' : 'headphone-toggle'} onClick={toggleHeadphoneEq}><Headphones size={17}/><span><small>HEADPHONE EQ</small><strong>{headphoneEq ? selectedHeadphone?.name || 'Enabled' : 'Off'}</strong></span><i/></button></div></div>
        </section>}

        {activeTab === 'equalizer' && <section className="eq-card standalone"><div className="eq-header"><div><p className="kicker">TONE SHAPING</p><h2>31-band equalizer</h2></div><div className="preset-row">{Object.keys(presets).map((name) => <button key={name} className={preset === name ? 'preset active' : 'preset'} onClick={() => choosePreset(name)}>{name}</button>)}{preset === 'Custom' && <button className="preset active">Custom</button>}</div></div><div className="eq-viewport"><div className="curve" aria-hidden="true"><svg viewBox="0 0 930 110" preserveAspectRatio="none"><polyline points={curve.map((y, index) => `${index * 31},${y}`).join(' ')}/></svg></div><div className="eq-grid">{bands.map((band, index) => <label className="eq-band" key={band}><span className="db">{eq[index] > 0 ? '+' : ''}{eq[index]}</span><input aria-label={`${band} Hz`} type="range" min="-12" max="12" step="1" value={eq[index]} onChange={(event) => updateBand(index, event.target.value)}/><span className="frequency">{band}</span></label>)}</div></div></section>}

        {activeTab === 'headphones' && <section className="utility-card headphone-card"><p className="kicker">HEADPHONE CORRECTION</p><div className="utility-heading"><div><h1>Headphone EQ</h1><p>Search the pinned AutoEq recommended catalog and load the model directly into PulseFX's native parametric correction engine.</p></div><button className={headphoneEq ? 'large-toggle on' : 'large-toggle'} onClick={toggleHeadphoneEq}><Power size={18}/>{headphoneEq ? 'Enabled' : 'Disabled'}</button></div><div className="headphone-profile-panel"><div className="headphone-current"><small>ACTIVE MODEL</small><strong>{selectedHeadphone?.name || 'No headphone selected'}</strong><span>{selectedHeadphone ? `${headphoneProfileInfo?.filters ?? '—'} filters · ${Number(headphoneProfileInfo?.preampDb ?? 0).toFixed(1)} dB profile preamp` : 'Choose your exact model for correction.'}</span></div><label className="headphone-search"><Search size={17}/><input aria-label="Search headphone models" placeholder="Search 6,033 headphone models…" value={headphoneQuery} onChange={(event) => setHeadphoneQuery(event.target.value)} onFocus={loadHeadphoneCatalog}/></label>{headphoneError && <div className="profile-error"><CircleAlert size={15}/><span>{headphoneError}</span></div>}<div className="headphone-results" role="listbox" aria-label="Headphone models">{headphoneLoading && headphoneModels.length === 0 && <div className="empty-state">Loading the pinned AutoEq catalog…</div>}{!headphoneLoading && headphoneModels.length === 0 && !headphoneError && <button className="refresh-button" onClick={loadHeadphoneCatalog}>Load headphone catalog</button>}{headphoneMatches.map((model) => <button key={model.path} role="option" aria-selected={selectedHeadphone?.path === model.path} className={selectedHeadphone?.path === model.path ? 'headphone-result selected' : 'headphone-result'} onClick={() => chooseHeadphone(model)} disabled={headphoneLoading}><span>{model.name}</span>{selectedHeadphone?.path === model.path && <strong>Active</strong>}</button>)}{headphoneModels.length > 0 && headphoneMatches.length === 0 && <div className="empty-state">No matching headphone in the pinned recommended catalog.</div>}</div></div><div className="info-grid"><div><small>PROCESSING ORDER</small><strong>Correction → EQ → Enhancement</strong></div><div><small>CATALOG</small><strong>{headphoneModels.length > 0 ? `${headphoneModels.length.toLocaleString()} recommended models` : '6,033 recommended models'}</strong></div><div><small>STATUS</small><strong>{headphoneEq && selectedHeadphone ? 'Correction active' : 'Transparent'}</strong></div></div><p className="muted-note">Profiles are pinned to AutoEq revision {headphoneRevision ? headphoneRevision.slice(0, 8) : '7ae0f56d'} for reproducibility. Invalid or missing correction data fails open instead of muting system audio.</p></section>}

        {activeTab === 'apps' && <section className="utility-card apps-card"><div className="utility-heading"><div><p className="kicker">APP VOLUME CONTROLLER</p><h1>Applications</h1><p>Control sessions currently playing through PulseFX Output.</p></div><button className="refresh-button" onClick={refreshApps}>Refresh</button></div><div className="app-list">{apps.length === 0 && <div className="empty-state">No active audio sessions are currently routed through PulseFX.</div>}{apps.map((app) => <div className="app-row" key={app.pid}><div className="app-identity"><div className="app-icon"><AudioLines size={16}/></div><div><strong>{app.name}</strong><span>PID {app.pid}</span></div></div><input aria-label={`${app.name} volume`} type="range" min="0" max="1" step="0.01" value={app.volume} onChange={(event) => changeAppVolume(app.pid, event.target.value)}/><span className="app-volume">{Math.round(app.volume * 100)}%</span><button aria-label={`${app.muted ? 'Unmute' : 'Mute'} ${app.name}`} className={app.muted ? 'mute-button muted' : 'mute-button'} onClick={() => toggleAppMute(app.pid, !app.muted)}>{app.muted ? <VolumeX size={17}/> : <Volume2 size={17}/>}</button></div>)}</div></section>}

        {activeTab === 'player' && <section className="utility-card media-card"><div className="utility-heading"><div><p className="kicker">AUDIO PLAYER</p><h1>Your music</h1><p>Play local audio through the same PulseFX system-wide enhancement pipeline and organize tracks into persistent playlists.</p></div><button className="large-toggle on" onClick={addAudioFiles}><Plus size={17}/>Add audio</button></div><div className="playlist-tools"><div className="playlist-tabs">{playlists.map((playlist) => <button key={playlist.id} className={playlist.id === activePlaylistId ? 'playlist-tab active' : 'playlist-tab'} onClick={() => selectPlaylist(playlist.id)}>{playlist.name}<span>{playlist.tracks.length}</span></button>)}</div><div className="playlist-create"><input aria-label="New playlist name" placeholder="New playlist" value={newPlaylistName} onChange={(event) => setNewPlaylistName(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter') createPlaylist(); }}/><button aria-label="Create playlist" onClick={createPlaylist}><Plus size={15}/></button><button aria-label="Delete active playlist" disabled={playlists.length <= 1} onClick={deleteActivePlaylist}><Trash2 size={15}/></button></div></div><div className="track-list">{!activePlaylist?.tracks?.length && <div className="empty-state">This playlist is empty. Add MP3, WAV, FLAC, M4A, AAC, OGG, Opus, or WebM audio.</div>}{activePlaylist?.tracks?.map((track, index) => <div className={mediaKind === 'local' && trackIndex === index ? 'track-row active' : 'track-row'} key={track.id}><button className="track-main" onClick={() => selectTrack(index)}><span className="track-number">{mediaKind === 'local' && trackIndex === index && isPlaying ? <AudioLines size={14}/> : index + 1}</span><strong>{track.name}</strong></button><button aria-label={`Remove ${track.name}`} className="track-remove" onClick={() => removeTrack(index)}><Trash2 size={14}/></button></div>)}</div></section>}

        {activeTab === 'radio' && <section className="utility-card media-card"><div className="utility-heading"><div><p className="kicker">INTERNET RADIO</p><h1>Live stations</h1><p>Search local and international stations from the open Radio Browser directory and play them through PulseFX.</p></div></div><div className="radio-search"><Search size={17}/><input aria-label="Search radio stations" placeholder="Search station name…" value={radioQuery} onChange={(event) => setRadioQuery(event.target.value)} onKeyDown={(event) => { if (event.key === 'Enter') refreshRadio(); }}/><button onClick={() => refreshRadio()} disabled={radioLoading}>{radioLoading ? 'Searching…' : 'Search'}</button></div>{radioError && <div className="profile-error"><CircleAlert size={15}/><span>{radioError}</span></div>}<div className="station-list">{radioLoading && radioStations.length === 0 && <div className="empty-state">Finding live stations…</div>}{!radioLoading && radioStations.length === 0 && !radioError && <div className="empty-state">No stations loaded.</div>}{radioStations.map((station) => <button key={station.stationuuid} className={activeStation?.stationuuid === station.stationuuid ? 'station-row active' : 'station-row'} onClick={() => playStation(station)}><div className="station-play">{activeStation?.stationuuid === station.stationuuid && isPlaying ? <AudioLines size={16}/> : <Play size={14}/>}</div><div><strong>{station.name}</strong><span>{[station.country || station.countrycode, station.tags?.split(',')[0], station.codec, station.bitrate ? `${station.bitrate} kbps` : ''].filter(Boolean).join(' · ')}</span></div></button>)}</div><p className="muted-note">Station discovery uses the free/open Radio Browser network and automatically fails over between its API servers.</p></section>}

        {activeTab === 'settings' && <section className="utility-card settings-card"><p className="kicker">QUICK CONTROLS</p><div className="utility-heading"><div><h1>Global hotkeys</h1><p>Click a shortcut field and press the key combination you want. PulseFX registers it globally, so it works while the main window is hidden in the system tray.</p></div></div><div className="shortcut-list">{Object.entries(shortcutLabels).map(([action, label]) => <label className="shortcut-row" key={action}><span><strong>{label}</strong><small>{action}</small></span><input aria-label={`${label} shortcut`} readOnly value={shortcuts[action] ?? ''} onKeyDown={(event) => { const value = acceleratorFromEvent(event); if (value) setShortcuts((current) => ({ ...current, [action]: value })); }}/></label>)}</div><button className="refresh-button settings-reset" onClick={() => setShortcuts(defaultShortcuts)}>Reset defaults</button><p className="muted-note">Quick Controls are also available from the PulseFX system-tray icon: processing, 3D Surround, EQ presets, Apps Volume Controller, Player, and Radio.</p></section>}

        {(activeTab === 'player' || activeTab === 'radio') && <section className={mediaSource ? 'transport-card active' : 'transport-card'}><div className="transport-meta"><div className="transport-art">{mediaKind === 'radio' ? <Globe2 size={20}/> : <Music2 size={20}/>}</div><div><strong>{mediaTitle || 'Nothing playing'}</strong><span>{mediaSubtitle || 'Choose a track or station'}</span></div></div><div className="transport-center"><div className="transport-buttons">{mediaKind === 'local' && <button aria-label="Previous track" onClick={previousTrack}><SkipBack size={17}/></button>}<button aria-label={isPlaying ? 'Pause' : 'Play'} className="transport-play" onClick={togglePlayback} disabled={!mediaSource}>{isPlaying ? <Pause size={18}/> : <Play size={18}/>}</button>{mediaKind === 'local' && <button aria-label="Next track" onClick={nextTrack}><SkipForward size={17}/></button>}</div>{mediaKind === 'local' ? <div className="transport-progress"><span>{formatTime(mediaTime)}</span><input aria-label="Playback position" type="range" min="0" max={Math.max(mediaDuration, 0)} step="0.1" value={Math.min(mediaTime, mediaDuration || 0)} onChange={(event) => seekMedia(event.target.value)}/><span>{formatTime(mediaDuration)}</span></div> : <div className="live-badge"><span/>LIVE</div>}</div><label className="transport-volume"><Volume2 size={16}/><input aria-label="Player volume" type="range" min="0" max="1" step="0.01" value={mediaVolume} onChange={(event) => setMediaVolume(clamp(event.target.value, 0, 1))}/></label></section>}

        <footer className="engine-footer"><span>Underruns <strong>{stats.underruns ?? 0}</strong></span><span>Overruns <strong>{stats.overruns ?? 0}</strong></span><span>Buffer <strong>{stats.bufferedFrames ?? 0} fr</strong></span><span>Clock <strong>{Number(stats.clockCorrectionPpm ?? 0).toFixed(0)} ppm</strong></span></footer>
      </div>
    </section>
  </main>;
}

createRoot(document.getElementById('root')).render(<React.StrictMode><App/></React.StrictMode>);