'use strict';
// DOScraft FM Lab: patch editor and player for the Nuked OPL3 AudioWorklet.
// The page keeps a shadow copy of every chip register and sends only changes.
(() => {
  // ------------------------------------------------------------------ data
  // Same rows as PATCHES in tools/build_music_pack.py: modulator then carrier
  // registers 20h, 40h, 60h, 80h, E0h, then C0h (feedback << 1 | connection).
  const GAME_PRESETS = [
    ['piano',  [0x01, 0x18, 0xF2, 0x54, 0x00], [0x01, 0x00, 0xF2, 0x37, 0x00], 0x06],
    ['epiano', [0x01, 0x1A, 0xF3, 0x35, 0x00], [0x01, 0x04, 0xF2, 0x24, 0x00], 0x06],
    ['bell',   [0x07, 0x18, 0xF5, 0x55, 0x00], [0x01, 0x00, 0xF3, 0x45, 0x00], 0x02],
    ['pad',    [0x21, 0x1E, 0x62, 0x14, 0x00], [0x21, 0x00, 0x52, 0x15, 0x00], 0x08],
    ['bass',   [0x01, 0x14, 0xF4, 0x56, 0x00], [0x01, 0x00, 0xF5, 0x58, 0x00], 0x06],
    ['organ',  [0x22, 0x0A, 0xF0, 0x0F, 0x00], [0x21, 0x00, 0xF0, 0x0F, 0x00], 0x01],
    ['pluck',  [0x01, 0x1C, 0xF6, 0x57, 0x00], [0x01, 0x00, 0xF6, 0x58, 0x00], 0x06],
    ['lead',   [0x21, 0x16, 0xF1, 0x16, 0x00], [0x21, 0x00, 0xF1, 0x16, 0x00], 0x0A],
    // A plain held sine: silent modulator, carrier on full.
    ['blank',  [0x21, 0x3F, 0xF0, 0x0F, 0x00], [0x21, 0x00, 0xF0, 0x07, 0x00], 0x00],
  ];
  const OP_OFFSET = [0, 1, 2, 8, 9, 10, 16, 17, 18];
  // F-numbers of C..B at block 4, as in src/platform/dos/opl_music.c.
  const FNUM = [345, 365, 387, 410, 435, 460, 488, 517, 547, 580, 615, 651];
  const MULT_X = [0.5, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 10, 12, 12, 15, 15];
  const KSL_OPTIONS = [[0, 'Off'], [2, '1.5 dB per octave'], [1, '3 dB per octave'], [3, '6 dB per octave']];
  const FB_TEXT = ['off', 'π/16', 'π/8', 'π/4', 'π/2', 'π', '2π', '4π'];
  // Approximate envelope times near middle C with KSR off (YM3812 manual tables).
  const ATTACK_MS = [Infinity, 2826, 1413, 707, 353, 177, 88.3, 44.2, 22.1, 11.0, 5.52, 2.76, 1.38, 0.69, 0.34, 0];
  const DECAY_MS = [Infinity, 39281, 19640, 9820, 4910, 2455, 1228, 614, 307, 153, 76.7, 38.4, 19.2, 9.59, 4.79, 2.40];
  const WAVE_NAMES = ['Sine', 'Half sine', 'Abs sine', 'Pulse sine', 'Alternating sine', 'Camel sine', 'Square', 'Log saw'];
  const OP_NAMES = ['Modulator', 'Carrier', 'Operator 3', 'Operator 4'];
  const ALG4_CHAINS = [[[1, 2, 3, 4]], [[1], [2, 3, 4]], [[1, 2], [3, 4]], [[1], [2, 3], [4]]];
  const ALG4_OUTPUTS = [[3], [0, 3], [1, 3], [0, 2, 3]];
  const STORE_PATCHES = 'doscraft-fm-lab:patches:v1';
  const STORE_CURRENT = 'doscraft-fm-lab:current:v1';
  const CHIP_RATE = 49716;

  const $ = (id) => document.getElementById(id);
  const hex2 = (n) => n.toString(16).toUpperCase().padStart(2, '0');
  const clone = (x) => JSON.parse(JSON.stringify(x));

  // ------------------------------------------------------------ patch model
  function opFromBytes(b20, b40, b60, b80, bE0) {
    return {
      am: (b20 >> 7) & 1, vib: (b20 >> 6) & 1, egt: (b20 >> 5) & 1, ksr: (b20 >> 4) & 1, mult: b20 & 15,
      ksl: (b40 >> 6) & 3, tl: b40 & 63, ar: b60 >> 4, dr: b60 & 15, sl: b80 >> 4, rr: b80 & 15, wf: bE0 & 7,
    };
  }
  const r20 = (o) => (o.am << 7) | (o.vib << 6) | (o.egt << 5) | (o.ksr << 4) | o.mult;
  const r40 = (o, extra = 0) => (o.ksl << 6) | Math.min(63, o.tl + extra);
  const r60 = (o) => (o.ar << 4) | o.dr;
  const r80 = (o) => (o.sl << 4) | o.rr;

  function defaultExtras() {
    return {
      opl3: false, fourOp: false, pan: 48, deepTrem: false, deepVib: false, wfx: [0, 0], alg4: 0,
      ops34: [opFromBytes(0x21, 0x3F, 0xF0, 0x0F, 0), opFromBytes(0x21, 0x3F, 0xF0, 0x0F, 0)],
    };
  }

  function patchFromBytes(name, bytes, extras) {
    return {
      name,
      ops: [opFromBytes(...bytes.slice(0, 5)), opFromBytes(...bytes.slice(5, 10))],
      fb: (bytes[10] >> 1) & 7,
      con: bytes[10] & 1,
      x: extras ? clone(extras) : defaultExtras(),
    };
  }

  // The 11 bytes the game would load. OPL2 only has waveforms 0-3.
  function gameBytes(p) {
    const [m, c] = p.ops;
    return [r20(m), r40(m), r60(m), r80(m), m.wf & 3, r20(c), r40(c), r60(c), r80(c), c.wf & 3, (p.fb << 1) | p.con];
  }

  function attenuation(velocity) {
    // build_music_pack.py's attenuation() with channel volume 100, expression 127.
    const level = (velocity / 127) * (100 / 127);
    if (level <= 0) return 63;
    return Math.max(0, Math.min(63, Math.round((-20 * Math.log10(level)) / 0.75)));
  }

  // ------------------------------------------------------------------ state
  let patch = patchFromBytes('piano', [...GAME_PRESETS[0][1], ...GAME_PRESETS[0][2], GAME_PRESETS[0][3]]);
  let loadedFrom = { name: 'piano', bytes: JSON.stringify([gameBytes(patch), patch.x]) };
  let octave = 3;
  let sustain = false;
  let velocity = 100;
  let master = 60;
  let serial = 0;
  let voices = [];

  // ------------------------------------------------------------- audio/chip
  let ctx = null;
  let node = null;
  let starting = null;
  let ready = false;
  const shadow = new Int16Array(0x200).fill(-1);
  let queue = [];
  let flushQueued = false;
  let wasmBytes = null;
  let latestScope = null;

  function decodeWasm() {
    const bin = atob(WASM_BASE64);
    const out = new Uint8Array(bin.length);
    for (let i = 0; i < bin.length; i++) out[i] = bin.charCodeAt(i);
    return out;
  }

  function write(reg, value, force) {
    if (!force && shadow[reg] === value) return;
    shadow[reg] = value;
    queue.push(reg, value);
    if (!flushQueued) {
      flushQueued = true;
      queueMicrotask(flush);
    }
  }

  function flush() {
    flushQueued = false;
    if (!node || !queue.length) return;
    node.port.postMessage({ writes: queue });
    queue = [];
  }

  function setStatus(text, isError) {
    const el = $('status');
    el.textContent = text;
    el.classList.toggle('err', !!isError);
  }

  function startAudio() {
    if (starting) return starting;
    starting = (async () => {
      const AC = window.AudioContext || window.webkitAudioContext;
      if (!AC) throw new Error('This browser has no Web Audio support.');
      try {
        ctx = new AC({ sampleRate: CHIP_RATE, latencyHint: 'interactive' });
      } catch (e) {
        ctx = new AC({ latencyHint: 'interactive' });
      }
      wasmBytes = wasmBytes || decodeWasm();
      let where = 'in an AudioWorklet';
      // Test switches: #js forces the wasm2js engine, #main the main-thread fallback.
      const force = location.hash.slice(1);
      if (force !== 'main' && await loadWorklet()) {
        node = new AudioWorkletNode(ctx, 'opl-processor', {
          numberOfInputs: 0, numberOfOutputs: 1, outputChannelCount: [2],
          processorOptions: { wasm: wasmBytes.buffer.slice(0), forceJs: force === 'js' },
        });
      } else {
        node = mainThreadNode();
        where = 'on the main thread (no AudioWorklet here)';
      }
      node.port.onmessage = (event) => {
        const msg = event.data;
        if (msg.scope) latestScope = msg.scope;
        else if (msg.ready) {
          ready = true;
          const native = Math.round(msg.sampleRate) === CHIP_RATE;
          $('rateText').textContent = (native
            ? 'Output rate: 49 716 Hz, the chip’s own'
            : `Output rate: ${Math.round(msg.sampleRate)} Hz (resampled by Nuked OPL3)`)
            + ` · ${msg.engine} ${where}`;
          setStatus('Sound is on. Play with the letter keys or the keyboard below.');
        } else if (msg.error) setStatus('The emulator failed to load: ' + msg.error, true);
      };
      node.connect(ctx.destination);
      if (ctx.state !== 'running') await ctx.resume();
      node.port.postMessage({ gain: gainValue() });
      shadow.fill(-1);
      queue = [];
      applyAll();
      flush();
      $('start').textContent = 'Sound on';
      $('start').setAttribute('aria-pressed', 'true');
      requestAnimationFrame(drawScope);
    })();
    starting.catch((e) => {
      setStatus('Could not start sound: ' + (e && e.message ? e.message : e), true);
      starting = null;
    });
    return starting;
  }

  async function loadWorklet() {
    if (!ctx.audioWorklet || !window.AudioWorkletNode) return false;
    const blobUrl = URL.createObjectURL(new Blob([WORKLET_SOURCE], { type: 'application/javascript' }));
    const dataUrl = 'data:application/javascript;base64,' + btoa(unescape(encodeURIComponent(WORKLET_SOURCE)));
    for (const url of [blobUrl, dataUrl]) {
      try {
        await ctx.audioWorklet.addModule(url);
        return true;
      } catch (e) {
        // Some pages allow only one of these URL forms; try the next.
      }
    }
    return false;
  }

  // Fallback when no AudioWorklet module can load: the same engine on the
  // main thread through a ScriptProcessorNode, behind a port-like interface.
  function mainThreadNode() {
    const sp = ctx.createScriptProcessor(2048, 0, 2);
    const port = { onmessage: null, postMessage: (msg) => engine.receive(msg) };
    const engine = new OplEngine(wasmBytes.buffer.slice(0), ctx.sampleRate,
      (msg) => { if (port.onmessage) port.onmessage({ data: msg }); }, location.hash === '#js');
    sp.onaudioprocess = (e) => engine.render(e.outputBuffer.getChannelData(0), e.outputBuffer.getChannelData(1));
    sp.port = port;
    return sp;
  }

  function gainValue() {
    return (master / 100) * 2;
  }

  // ------------------------------------------------------------- voices
  function buildVoices() {
    const x = patch.x;
    const four = x.opl3 && x.fourOp;
    const list = four
      ? [[0, 0], [0, 1], [0, 2], [0x100, 0], [0x100, 1], [0x100, 2]]
      : [0, 1, 2, 3, 4, 5, 6, 7, 8].map((c) => [0, c]);
    voices = list.map(([bank, ch]) => ({ bank, ch, note: -1, keyed: false, sustained: false, att: 0, src: null, started: 0, released: 0, b0: 0 }));
    $('polyText').textContent = four ? '6 four-operator voices (OPL3 only)' : '9 voices, as in the game';
    const leds = $('voiceLeds');
    leds.textContent = '';
    voices.forEach(() => {
      const led = document.createElement('span');
      led.className = 'voice-led';
      leds.appendChild(led);
    });
    updateLeds();
  }

  function updateLeds() {
    const leds = $('voiceLeds').children;
    let used = 0;
    voices.forEach((v, i) => {
      leds[i].className = 'voice-led' + (v.keyed ? (v.sustained ? ' sus' : ' on') : '');
      if (v.keyed) used++;
    });
    $('voiceLeds').setAttribute('aria-label', `Voices in use: ${used} of ${voices.length}`);
  }

  function effectiveWave(i) {
    const x = patch.x;
    if (i >= 2) return patch.x.ops34[i - 2].wf;
    const op = patch.ops[i];
    return x.opl3 && x.wfx[i] ? x.wfx[i] : op.wf & 3;
  }

  function writeVoicePatch(v) {
    const x = patch.x;
    const four = x.opl3 && x.fourOp;
    const ops = four ? [...patch.ops, ...x.ops34] : patch.ops;
    const slots = [OP_OFFSET[v.ch], OP_OFFSET[v.ch] + 3];
    if (four) slots.push(OP_OFFSET[v.ch + 3], OP_OFFSET[v.ch + 3] + 3);
    // Velocity attenuates the operators that reach the output. In two-operator
    // mode only the carrier, as the game does even for additive patches.
    const outputs = four ? ALG4_OUTPUTS[x.alg4] : [1];
    ops.forEach((op, i) => {
      const base = v.bank + slots[i];
      write(base + 0x20, r20(op));
      write(base + 0x40, r40(op, outputs.includes(i) ? v.att : 0));
      write(base + 0x60, r60(op));
      write(base + 0x80, r80(op));
      write(base + 0xE0, effectiveWave(i));
    });
    const pan = x.opl3 ? x.pan : 0;
    if (four) {
      write(v.bank + 0xC0 + v.ch, (patch.fb << 1) | (x.alg4 & 1) | pan);
      write(v.bank + 0xC0 + v.ch + 3, (x.alg4 >> 1) | pan);
    } else {
      write(v.bank + 0xC0 + v.ch, (patch.fb << 1) | patch.con | pan);
    }
  }

  function applyAll() {
    const x = patch.x;
    write(0x01, 0x20);
    write(0x08, 0x00);
    write(0x105, x.opl3 ? 1 : 0);
    write(0x104, x.opl3 && x.fourOp ? 0x3F : 0);
    write(0xBD, (x.deepTrem ? 0x80 : 0) | (x.deepVib ? 0x40 : 0));
    voices.forEach(writeVoicePatch);
  }

  function keyOff(v) {
    if (!v.keyed) return;
    v.keyed = false;
    v.sustained = false;
    v.src = null;
    v.released = ++serial;
    write(v.bank + 0xB0 + v.ch, v.b0 & ~0x20, true);
  }

  async function noteOn(midi, vel, src) {
    let shortNote = false;
    if (!node) {
      try { await startAudio(); } catch (e) { return; }
      shortNote = src && !held.has(src); // released while sound was starting
    }
    noteOffSrc(src);
    let note = midi;
    while (note < 12) note += 12;
    while (note > 107) note -= 12;
    // A note still ringing under the sustain pedal is retriggered in place.
    const same = voices.find((x) => x.keyed && x.sustained && x.note === note);
    const free = voices.filter((x) => !x.keyed);
    const v = same || (free.length
      ? free.reduce((a, b) => (a.released <= b.released ? a : b))
      : voices.reduce((a, b) => (a.started <= b.started ? a : b)));
    v.note = note;
    v.att = attenuation(vel);
    v.src = src;
    v.keyed = true;
    v.sustained = false;
    v.started = ++serial;
    let block = Math.floor(note / 12) - 1;
    const fnum = FNUM[note % 12];
    block = Math.max(0, Math.min(7, block));
    const lo = v.bank + v.ch;
    // Same order as the game: key off, carrier level, F-number, key on.
    write(lo + 0xB0, (block << 2) | (fnum >> 8), true);
    writeVoicePatch(v);
    write(lo + 0xA0, fnum & 255);
    v.b0 = 0x20 | (block << 2) | (fnum >> 8);
    write(lo + 0xB0, v.b0, true);
    pianoMark(midi, true);
    updateLeds();
    if (shortNote) {
      setTimeout(() => {
        if (held.has(src)) return;
        pianoMark(midi, false);
        noteOffSrc(src);
      }, 250);
    }
  }

  function noteOffSrc(src) {
    if (!src) return;
    voices.forEach((v) => {
      if (v.src !== src || !v.keyed) return;
      if (sustain) {
        v.sustained = true;
        v.src = null;
      } else keyOff(v);
    });
    updateLeds();
  }

  function setSustain(on) {
    sustain = on;
    $('sustain').setAttribute('aria-pressed', String(on));
    if (!on) voices.forEach((v) => { if (v.sustained) keyOff(v); });
    updateLeds();
  }

  function panic() {
    held.clear();
    pointerNotes.clear();
    document.querySelectorAll('.piano .down').forEach((k) => k.classList.remove('down'));
    buildVoices();
    if (node) {
      // Reset the chip: release tails stop at once.
      node.port.postMessage({ reset: true });
      shadow.fill(-1);
      queue = [];
      applyAll();
      flush();
    }
    setSustain(false);
  }

  // ------------------------------------------------------------- patch edits
  function patchChanged(opts = {}) {
    if (opts.rebuild) panic();
    else applyAll();
    renderHex();
    renderAlgo();
    renderEdited();
    renderExtrasState();
    for (let i = 0; i < 4; i++) drawEnvelope(i);
    saveCurrent();
  }

  function loadPatch(p, from) {
    const modeBefore = JSON.stringify([patch.x.opl3, patch.x.fourOp]);
    patch = clone(p);
    if (!patch.x) patch.x = defaultExtras();
    loadedFrom = { name: from || patch.name, bytes: JSON.stringify([gameBytes(patch), patch.x]) };
    refreshControls();
    $('patchName').value = patch.name || '';
    patchChanged({ rebuild: modeBefore !== JSON.stringify([patch.x.opl3, patch.x.fourOp]) });
    renderPresetButtons();
  }

  function loadGameBytes(name, bytes) {
    // Game patches keep the chip mode but drop per-operator OPL3 waveforms.
    const x = clone(patch.x);
    x.wfx = [0, 0];
    loadPatch(patchFromBytes(name, bytes, x), name);
  }

  // ----------------------------------------------------------- UI builders
  const controls = [];

  function msText(ms) {
    if (ms < 1) return `${ms.toFixed(2)} ms`;
    if (ms < 1000) return `${Math.round(ms)} ms`;
    return `${(ms / 1000).toFixed(1)} s`;
  }

  const FORMATS = {
    mult: (v) => `×${MULT_X[v]}`,
    tl: (v) => v === 0 ? '0 dB' : `−${(v * 0.75).toFixed(2).replace(/\.?0+$/, '')} dB`,
    ar: (v) => (v === 0 ? 'never' : v === 15 ? 'instant' : `≈${msText(ATTACK_MS[v])}`),
    dr: (v) => (v === 0 ? 'hold' : `≈${msText(DECAY_MS[v])}`),
    sl: (v) => (v === 15 ? '−93 dB' : v === 0 ? '0 dB' : `−${v * 3} dB`),
    rr: (v) => (v === 0 ? 'hold' : `≈${msText(DECAY_MS[v])}`),
    fb: (v) => FB_TEXT[v],
  };
  const SPOKEN = {
    mult: 'times', tl: 'attenuation', ar: 'attack time', dr: 'time to fall 96 decibels', sl: 'sustain level',
    rr: 'time to fall 96 decibels', fb: 'feedback',
  };

  function makeSlider(host, id, label, max, get, set, fmtKey) {
    host.classList.add('slider');
    const lab = document.createElement('label');
    lab.htmlFor = id;
    lab.textContent = label;
    const input = document.createElement('input');
    input.type = 'range';
    input.id = id;
    input.min = 0;
    input.max = max;
    input.step = 1;
    const out = document.createElement('output');
    out.htmlFor = id;
    host.append(lab, input, out);
    const show = () => {
      const v = get();
      input.value = v;
      const text = FORMATS[fmtKey](v);
      out.innerHTML = `${text} <span class="raw">(${v})</span>`;
      input.setAttribute('aria-valuetext', `${v}, ${text.replace('≈', 'about ').replace('−', 'minus ')}`);
    };
    input.addEventListener('input', () => {
      set(Number(input.value));
      show();
      patchChanged();
    });
    controls.push(show);
    return input;
  }

  function makeToggle(host, id, label, hint, get, set) {
    const lab = document.createElement('label');
    lab.className = 'toggle';
    const input = document.createElement('input');
    input.type = 'checkbox';
    input.id = id;
    const span = document.createElement('span');
    span.innerHTML = `${label}<small>${hint}</small>`;
    lab.append(input, span);
    host.appendChild(lab);
    input.addEventListener('change', () => {
      set(input.checked ? 1 : 0);
      patchChanged();
    });
    controls.push(() => { input.checked = !!get(); });
    return input;
  }

  function waveSvg(w) {
    const pts = [];
    for (let i = 0; i <= 64; i++) {
      const p = i / 64;
      const s = Math.sin(2 * Math.PI * p);
      let y;
      switch (w) {
        case 0: y = s; break;
        case 1: y = p < 0.5 ? s : 0; break;
        case 2: y = Math.abs(s); break;
        case 3: y = (p % 0.5) < 0.25 ? Math.abs(s) : 0; break;
        case 4: y = p < 0.5 ? Math.sin(4 * Math.PI * p) : 0; break;
        case 5: y = p < 0.5 ? Math.abs(Math.sin(4 * Math.PI * p)) : 0; break;
        case 6: y = p < 0.5 ? 1 : -1; break;
        default: y = p < 0.5 ? Math.exp(-5 * p * 2) : -Math.exp(-5 * (1 - p) * 2);
      }
      pts.push(`${(4 + p * 64).toFixed(1)},${(17 - y * 13).toFixed(1)}`);
    }
    return `<svg viewBox="0 0 72 34" aria-hidden="true"><line x1="2" y1="17" x2="70" y2="17"></line><path d="M${pts.join('L')}"></path></svg>`;
  }

  function makeWaves(host, name, choices, get, set, labelFor) {
    const box = document.createElement('div');
    box.className = 'waves';
    box.setAttribute('role', 'radiogroup');
    const inputs = [];
    choices.forEach((w) => {
      const cell = document.createElement('div');
      cell.className = 'wave';
      const id = `${name}-${w}`;
      const input = document.createElement('input');
      input.type = 'radio';
      input.name = name;
      input.id = id;
      input.value = w;
      const lab = document.createElement('label');
      lab.htmlFor = id;
      lab.innerHTML = labelFor(w);
      cell.append(input, lab);
      box.appendChild(cell);
      input.addEventListener('change', () => {
        if (input.checked) {
          set(w);
          patchChanged();
        }
      });
      inputs.push(input);
    });
    host.appendChild(box);
    controls.push(() => {
      const v = get();
      inputs.forEach((inp) => { inp.checked = Number(inp.value) === v; });
    });
    return { box, inputs };
  }

  function buildOperator(host, i, op, waveChoices) {
    const id = (f) => `op${i}-${f}`;
    const board = document.createElement('section');
    board.className = 'op-board';
    board.setAttribute('aria-labelledby', id('title'));
    const slotNote = i < 2 ? `20h–E0h + ${i === 0 ? '0' : '3'} (channel 0 slots ${i === 0 ? '00' : '03'})` : '';
    const blurb = [
      'Shapes the tone. Lower total level number = more modulation = brighter.',
      'The operator you hear. Its envelope is the note’s volume shape.',
      'Second pair, first operator.',
      'Second pair, second operator.',
    ][i];
    board.innerHTML = `
      <div class="op-title"><h2 id="${id('title')}">${OP_NAMES[i]}${i < 2 ? ` <span class="small muted">operator ${i + 1}</span>` : ''}</h2><span class="regs">${slotNote}</span></div>
      <p class="small muted">${blurb}</p>
      <fieldset class="group" data-g="level"><legend class="label-caps">Level and tuning · 20h 40h</legend></fieldset>
      <fieldset class="group" data-g="env"><legend class="label-caps">Envelope · 60h 80h</legend></fieldset>
      <fieldset class="group" data-g="wave"><legend class="label-caps">Waveform · E0h</legend></fieldset>
      <fieldset class="group" data-g="mod"><legend class="label-caps">Scaling and modulation · 20h 40h</legend></fieldset>`;
    const g = (k) => board.querySelector(`[data-g="${k}"]`);
    const row = (k) => { const d = document.createElement('div'); g(k).appendChild(d); return d; };
    const o = () => op();
    makeSlider(row('level'), id('mult'), 'Multiplier', 15, () => o().mult, (v) => { o().mult = v; }, 'mult');
    makeSlider(row('level'), id('tl'), 'Total level', 63, () => o().tl, (v) => { o().tl = v; }, 'tl');
    makeSlider(row('env'), id('ar'), 'Attack', 15, () => o().ar, (v) => { o().ar = v; }, 'ar');
    makeSlider(row('env'), id('dr'), 'Decay', 15, () => o().dr, (v) => { o().dr = v; }, 'dr');
    makeSlider(row('env'), id('sl'), 'Sustain level', 15, () => o().sl, (v) => { o().sl = v; }, 'sl');
    makeSlider(row('env'), id('rr'), 'Release', 15, () => o().rr, (v) => { o().rr = v; }, 'rr');
    const envToggles = row('env');
    envToggles.className = 'toggles';
    makeToggle(envToggles, id('egt'), 'Sustain hold (EG type)', 'Hold at the sustain level while the key is down', () => o().egt, (v) => { o().egt = v; });
    const canvas = document.createElement('canvas');
    canvas.className = 'env-canvas';
    canvas.id = id('env');
    canvas.width = 600;
    canvas.height = 140;
    canvas.setAttribute('role', 'img');
    g('env').appendChild(canvas);
    const envNote = document.createElement('p');
    envNote.className = 'small muted';
    envNote.textContent = 'Decay and release times are for a full 96 dB fall. The graph shows a key held for one second.';
    g('env').appendChild(envNote);
    makeWaves(g('wave'), id('wf'), waveChoices, () => o().wf, (v) => { o().wf = v; },
      (w) => `${waveSvg(w)}<span>${w} · ${WAVE_NAMES[w]}</span>`);
    const mod = row('mod');
    mod.className = 'toggles';
    makeToggle(mod, id('ksr'), 'KSR', 'Faster envelope on higher notes', () => o().ksr, (v) => { o().ksr = v; });
    makeToggle(mod, id('am'), 'Tremolo (AM)', 'Volume wobble, 3.7 Hz', () => o().am, (v) => { o().am = v; });
    makeToggle(mod, id('vib'), 'Vibrato (VIB)', 'Pitch wobble, 6.1 Hz', () => o().vib, (v) => { o().vib = v; });
    const kslRow = document.createElement('div');
    kslRow.className = 'select-row';
    kslRow.innerHTML = `<label for="${id('ksl')}">KSL</label><select id="${id('ksl')}">${KSL_OPTIONS.map(([v, t]) => `<option value="${v}">${t} quieter going up</option>`).join('')}</select>`;
    g('mod').appendChild(kslRow);
    const ksl = kslRow.querySelector('select');
    ksl.addEventListener('change', () => { o().ksl = Number(ksl.value); patchChanged(); });
    controls.push(() => { ksl.value = String(o().ksl); });
    host.appendChild(board);
  }

  function buildChannel() {
    makeSlider(document.querySelector('[data-ctl="fb"]'), 'fb', 'Feedback', 7, () => patch.fb, (v) => { patch.fb = v; }, 'fb');
    ['conFm', 'conAdd'].forEach((id) => {
      $(id).addEventListener('change', () => { patch.con = Number($(id).value); patchChanged(); });
    });
    controls.push(() => { $('conFm').checked = patch.con === 0; $('conAdd').checked = patch.con === 1; });
  }

  function buildExtras() {
    const x = () => patch.x;
    $('opl3Mode').addEventListener('change', () => {
      x().opl3 = $('opl3Mode').checked;
      if (!x().opl3) x().fourOp = false;
      patchChanged({ rebuild: true });
    });
    $('fourOp').addEventListener('change', () => { x().fourOp = $('fourOp').checked; patchChanged({ rebuild: true }); });
    $('pan').addEventListener('change', () => { x().pan = Number($('pan').value); patchChanged(); });
    $('deepTrem').addEventListener('change', () => { x().deepTrem = $('deepTrem').checked; patchChanged(); });
    $('deepVib').addEventListener('change', () => { x().deepVib = $('deepVib').checked; patchChanged(); });
    $('alg4').addEventListener('change', () => { x().alg4 = Number($('alg4').value); patchChanged(); });
    controls.push(() => {
      $('opl3Mode').checked = x().opl3;
      $('fourOp').checked = x().fourOp;
      $('pan').value = String(x().pan);
      $('deepTrem').checked = x().deepTrem;
      $('deepVib').checked = x().deepVib;
      $('alg4').value = String(x().alg4);
    });
    [0, 1].forEach((i) => {
      const box = document.createElement('div');
      box.className = 'subpanel';
      box.innerHTML = `<p class="label-caps">${OP_NAMES[i]}</p>`;
      $('wfxTwo').appendChild(box);
      const made = makeWaves(box, `wfx${i}`, [0, 4, 5, 6, 7], () => x().wfx[i], (v) => { x().wfx[i] = v; },
        (w) => (w === 0 ? `<svg viewBox="0 0 72 34" aria-hidden="true"><line x1="2" y1="17" x2="70" y2="17"></line></svg><span>Main (0–3)</span>` : `${waveSvg(w)}<span>${w} · ${WAVE_NAMES[w]}</span>`));
      made.box.classList.add('wfx');
    });
    [0, 1].forEach((k) => buildOperator($('ops34'), k + 2, () => x().ops34[k], [0, 1, 2, 3, 4, 5, 6, 7]));
  }

  function renderExtrasState() {
    const x = patch.x;
    $('fourOp').disabled = !x.opl3;
    $('pan').disabled = !x.opl3;
    $('fourOpPanel').disabled = !(x.opl3 && x.fourOp);
    document.querySelectorAll('.wfx input').forEach((inp) => { inp.disabled = !x.opl3; });
  }

  function refreshControls() {
    controls.forEach((show) => show());
    renderExtrasState();
  }

  // --------------------------------------------------------- renderers
  function renderHex() {
    const b = gameBytes(patch).map(hex2);
    $('hexLine').innerHTML = `<span class="op">${b.slice(0, 5).join(' ')}</span><span class="sep">/</span><span class="op">${b.slice(5, 10).join(' ')}</span><span class="sep">/</span><span class="c0">${b[10]}</span>`;
    const x = patch.x;
    const notes = [];
    if (x.opl3) {
      [0, 1].forEach((i) => {
        if (x.wfx[i]) notes.push(`${OP_NAMES[i].toLowerCase()} uses OPL3 waveform ${x.wfx[i]}; the game plays waveform ${patch.ops[i].wf & 3}`);
      });
      if (x.fourOp) notes.push('four-operator voices are on; the game plays only the modulator and carrier');
      if (x.pan !== 48) notes.push('stereo pan is set; the game is mono');
    }
    if (x.deepTrem || x.deepVib) notes.push('deep tremolo or vibrato is on; the game keeps the shallow depths');
    let text = notes.length ? `What you hear is not what the game plays: ${notes.join('; ')}. The hex line shows the game's version.` : '';
    if (patch.ops[1].ksl) {
      text += (text ? ' ' : '') + 'Carrier KSL is set, but DOScraft’s player rewrites the carrier’s 40h on every note without the KSL bits, so the game currently plays it with KSL off.';
    }
    $('gameWarn').hidden = !text;
    $('gameWarn').textContent = text;
  }

  function renderEdited() {
    const now = JSON.stringify([gameBytes(patch), patch.x]);
    $('editedTag').hidden = now === loadedFrom.bytes;
  }

  function renderPresetButtons() {
    const host = $('presets');
    host.querySelectorAll('button').forEach((btn) => {
      btn.setAttribute('aria-pressed', String(btn.dataset.name === loadedFrom.name));
    });
  }

  function renderAlgo() {
    const svg = $('algo');
    const four = patch.x.opl3 && patch.x.fourOp;
    const chains = four ? ALG4_CHAINS[patch.x.alg4] : patch.con ? [[1], [2]] : [[1, 2]];
    const label = (n) => (four ? String(n) : n === 1 ? 'MOD' : 'CAR');
    const bw = four ? 34 : 50;
    const gap = four ? 14 : 26;
    const rowH = 32;
    const top = 26;
    const outX = 210;
    let s = '';
    const ends = [];
    chains.forEach((chain, r) => {
      const y = top + r * rowH + (3 - chains.length) * rowH / 2;
      chain.forEach((n, k) => {
        const x = 8 + k * (bw + gap);
        s += `<rect class="box" x="${x}" y="${y - 11}" width="${bw}" height="22" rx="3"></rect><text x="${x + bw / 2}" y="${y + 4.5}" text-anchor="middle">${label(n)}</text>`;
        if (k > 0) s += `<path class="wire" d="M${x - gap} ${y}H${x - 2}"></path><path class="wire" d="M${x - 6} ${y - 4}L${x - 2} ${y}L${x - 6} ${y + 4}"></path>`;
        if (n === 1) {
          s += `<path class="fb" d="M${x + bw} ${y - 4}h6v-12h-${bw + 12}v12h6"></path><text class="fbtext" x="${x + bw / 2}" y="${y - 19}" text-anchor="middle">FB ${patch.fb}</text>`;
        }
      });
      const endX = 8 + (chain.length - 1) * (bw + gap) + bw;
      ends.push([endX, y]);
    });
    const sumY = top + rowH;
    ends.forEach(([x, y]) => { s += `<path class="wire" d="M${x} ${y}H${outX - 24}V${sumY}"></path>`; });
    s += `<circle class="box" cx="${outX - 14}" cy="${sumY}" r="10"></circle><text x="${outX - 14}" y="${sumY + 4.5}" text-anchor="middle">+</text>`;
    s += `<path class="wire" d="M${outX - 4} ${sumY}H${outX + 10}"></path><text x="${outX + 14}" y="${sumY + 4.5}">OUT</text>`;
    svg.innerHTML = s;
    const desc = four
      ? $('alg4').selectedOptions[0].textContent
      : patch.con ? 'Additive: modulator and carrier both heard' : 'FM: modulator feeds the carrier';
    svg.setAttribute('aria-label', `Operator wiring. ${desc}. Feedback ${FB_TEXT[patch.fb]}.`);
  }

  function cssVar(name) {
    return getComputedStyle(document.documentElement).getPropertyValue(name).trim();
  }

  // Match the drawing buffer to the displayed size so lines and text stay crisp.
  function fitCanvas(canvas) {
    const dpr = window.devicePixelRatio || 1;
    const w = Math.max(1, Math.round(canvas.clientWidth * dpr));
    const h = Math.max(1, Math.round(canvas.clientHeight * dpr));
    if (canvas.width !== w || canvas.height !== h) {
      canvas.width = w;
      canvas.height = h;
    }
    return canvas.getContext('2d');
  }

  function envelopeLevels(op, holdMs, totalMs, steps) {
    // dB below full (0..96) at each step; key down for holdMs.
    const out = [];
    let level = 96;
    let phase = op.ar === 0 ? 'off' : 'attack';
    const slDb = op.sl === 15 ? 93 : op.sl * 3;
    const dt = totalMs / steps;
    for (let i = 0; i <= steps; i++) {
      const t = i * dt;
      if (t >= holdMs && phase !== 'release') phase = 'release';
      if (phase === 'attack') {
        const ta = ATTACK_MS[op.ar];
        const f = ta === 0 ? 1 : Math.min(1, t / ta);
        level = 96 * Math.pow(1 - f, 3);
        if (f >= 1) { level = 0; phase = 'decay'; }
      } else if (phase === 'decay') {
        if (op.dr) level = Math.min(slDb, level + (96 / DECAY_MS[op.dr]) * dt);
        if (level >= slDb) phase = op.egt ? 'sustain' : 'fade';
      } else if (phase === 'fade') {
        if (op.rr) level = Math.min(96, level + (96 / DECAY_MS[op.rr]) * dt);
      } else if (phase === 'release') {
        if (op.rr) level = Math.min(96, level + (96 / DECAY_MS[op.rr]) * dt);
      }
      out.push(level);
    }
    return out;
  }

  function drawEnvelope(i) {
    const canvas = $(`op${i}-env`);
    if (!canvas) return;
    const op = i < 2 ? patch.ops[i] : patch.x.ops34[i - 2];
    const c = fitCanvas(canvas);
    const W = canvas.width;
    const H = canvas.height;
    const px = W / canvas.clientWidth || 1;
    const hold = 1000;
    const total = 2000;
    const lv = envelopeLevels(op, hold, total, 400);
    c.clearRect(0, 0, W, H);
    c.strokeStyle = cssVar('--line');
    c.lineWidth = 1;
    c.font = `${Math.round(11 * px)}px ${cssVar('--font-mono') || 'monospace'}`;
    c.fillStyle = cssVar('--muted');
    [0, 48, 96].forEach((db) => {
      const y = 8 + (db / 96) * (H - 16);
      c.beginPath(); c.moveTo(0, y); c.lineTo(W, y); c.stroke();
    });
    const offX = (hold / total) * W;
    c.setLineDash([6, 6]);
    c.beginPath(); c.moveTo(offX, 0); c.lineTo(offX, H); c.stroke();
    c.setLineDash([]);
    c.fillText('key up', offX + 6 * px, H - 8 * px);
    c.fillText('0 dB', 6 * px, 20 * px);
    c.strokeStyle = cssVar('--accent');
    c.lineWidth = 2.5 * px;
    c.beginPath();
    lv.forEach((l, k) => {
      const x = (k / (lv.length - 1)) * W;
      const y = 8 + (l / 96) * (H - 16);
      if (k) c.lineTo(x, y); else c.moveTo(x, y);
    });
    c.stroke();
    const slDb = op.sl === 15 ? 93 : op.sl * 3;
    canvas.setAttribute('aria-label', `${OP_NAMES[i]} envelope: attack ${FORMATS.ar(op.ar)}, decay ${FORMATS.dr(op.dr)} toward minus ${slDb} dB, ${op.egt ? 'held while the key is down' : 'keeps fading while the key is down'}, release ${FORMATS.rr(op.rr)}.`);
  }

  function drawScope() {
    const canvas = $('scope');
    const c = fitCanvas(canvas);
    const W = canvas.width;
    const H = canvas.height;
    c.clearRect(0, 0, W, H);
    c.strokeStyle = cssVar('--line');
    c.lineWidth = 1;
    c.beginPath(); c.moveTo(0, H / 2); c.lineTo(W, H / 2); c.stroke();
    const s = latestScope;
    if (s) {
      // Trigger on a rising zero crossing so the trace stands still.
      let start = 0;
      for (let i = 1; i < s.length - 1024; i++) {
        if (s[i - 1] < 0 && s[i] >= 0) { start = i; break; }
      }
      const n = 1024;
      c.strokeStyle = cssVar('--trace');
      c.lineWidth = 2 * (W / canvas.clientWidth || 1);
      c.beginPath();
      for (let i = 0; i < n; i++) {
        const x = (i / (n - 1)) * W;
        const y = H / 2 - s[start + i] * (H / 2) * 3.2;
        if (i) c.lineTo(x, y); else c.moveTo(x, y);
      }
      c.stroke();
    }
    requestAnimationFrame(drawScope);
  }

  // ----------------------------------------------------------- presets
  function buildPresets() {
    const host = $('presets');
    GAME_PRESETS.forEach(([name, mod, car, c0]) => {
      const btn = document.createElement('button');
      btn.type = 'button';
      btn.dataset.name = name;
      btn.textContent = name === 'blank' ? 'blank (sine)' : name;
      btn.title = [...mod, ...car, c0].map(hex2).join(' ');
      btn.addEventListener('click', () => loadGameBytes(name, [...mod, ...car, c0]));
      host.appendChild(btn);
    });
  }

  // ------------------------------------------------------ hex in and out
  function parseHex(text) {
    const quoted = text.match(/['"]([^'"]*)['"]/);
    const stripped = text.replace(/'[^']*'|"[^"]*"/g, ' ');
    const tokens = stripped.match(/\b(?:0[xX])?[0-9a-fA-F]{1,2}\b/g) || [];
    if (tokens.length !== 11) {
      throw new Error(`Found ${tokens.length} bytes. A patch is 11 hex bytes: modulator 20 40 60 80 E0, carrier 20 40 60 80 E0, then C0.`);
    }
    const bytes = tokens.map((t) => parseInt(t.replace(/^0x/i, ''), 16));
    return { name: quoted ? quoted[1] : null, bytes };
  }

  function pyName(name) {
    const n = (name || 'patch').toLowerCase().replace(/[^a-z0-9_]+/g, '_').replace(/^_+|_+$/g, '') || 'patch';
    return n;
  }

  function pythonRow() {
    const b = gameBytes(patch).map((v) => '0x' + hex2(v));
    const head = `('${pyName(patch.name)}',`;
    return `    ${head.padEnd(10)} (${b.slice(0, 5).join(', ')}), (${b.slice(5, 10).join(', ')}), ${b[10]}),`;
  }

  async function copyText(text, button) {
    const old = button.textContent;
    try {
      await navigator.clipboard.writeText(text);
      button.textContent = 'Copied';
    } catch (e) {
      const sel = window.getSelection();
      const range = document.createRange();
      range.selectNodeContents($('hexLine'));
      sel.removeAllRanges();
      sel.addRange(range);
      button.textContent = 'Select and copy';
      $('pasteMsg').textContent = `Copying is blocked here. The text is: ${text}`;
    }
    setTimeout(() => { button.textContent = old; }, 1600);
  }

  function loadPasted() {
    const msg = $('pasteMsg');
    try {
      const { name, bytes } = parseHex($('pasteHex').value);
      loadGameBytes(name || patch.name || 'pasted', bytes);
      msg.classList.remove('err');
      msg.textContent = `Loaded ${bytes.map(hex2).join(' ')}${name ? ` as ${name}` : ''}.`;
    } catch (e) {
      msg.classList.add('err');
      msg.textContent = e.message;
    }
  }

  // ------------------------------------------------------------- SBI files
  // 52 bytes: "SBI" 1Ah, 32-byte name, then modulator/carrier pairs of 20h,
  // 40h, 60h, 80h, E0h, then C0h and 5 unused bytes.
  function sbiToBytes(buf) {
    const b = new Uint8Array(buf);
    const magic = String.fromCharCode(...b.slice(0, 4));
    if (magic === '4OP\x1A') throw new Error('four-operator SBI files are not supported');
    if (magic !== 'SBI\x1A') throw new Error('not an SBI file (it should start with "SBI" and 1Ah)');
    if (b.length < 47) throw new Error(`only ${b.length} bytes; an SBI instrument needs at least 47`);
    let name = '';
    for (let i = 4; i < 36 && b[i]; i++) name += String.fromCharCode(b[i]);
    const r = b.slice(36, 47);
    return { name: name.trim(), bytes: [r[0], r[2], r[4], r[6], r[8], r[1], r[3], r[5], r[7], r[9], r[10]] };
  }

  function bytesToSbi(name, g) {
    const out = new Uint8Array(52);
    out.set([0x53, 0x42, 0x49, 0x1A]);
    const n = (name || 'patch').replace(/[^\x20-\x7E]/g, '?').slice(0, 31);
    for (let i = 0; i < n.length; i++) out[4 + i] = n.charCodeAt(i);
    out.set([g[0], g[5], g[1], g[6], g[2], g[7], g[3], g[8], g[4], g[9], g[10]], 36);
    return out;
  }

  const CRC_TABLE = (() => {
    const t = new Uint32Array(256);
    for (let n = 0; n < 256; n++) {
      let c = n;
      for (let k = 0; k < 8; k++) c = c & 1 ? 0xEDB88320 ^ (c >>> 1) : c >>> 1;
      t[n] = c >>> 0;
    }
    return t;
  })();

  function crc32(data) {
    let c = 0xFFFFFFFF;
    for (const byte of data) c = CRC_TABLE[(c ^ byte) & 0xFF] ^ (c >>> 8);
    return (c ^ 0xFFFFFFFF) >>> 0;
  }

  // One stored (uncompressed) file in a zip archive.
  function zipOne(filename, data) {
    const name = new TextEncoder().encode(filename);
    const crc = crc32(data);
    const local = new DataView(new ArrayBuffer(30));
    local.setUint32(0, 0x04034b50, true); local.setUint16(4, 20, true); local.setUint16(12, 0x21, true);
    local.setUint32(14, crc, true); local.setUint32(18, data.length, true); local.setUint32(22, data.length, true);
    local.setUint16(26, name.length, true);
    const central = new DataView(new ArrayBuffer(46));
    central.setUint32(0, 0x02014b50, true); central.setUint16(4, 20, true); central.setUint16(6, 20, true);
    central.setUint16(14, 0x21, true); central.setUint32(16, crc, true); central.setUint32(20, data.length, true);
    central.setUint32(24, data.length, true); central.setUint16(28, name.length, true);
    const localSize = 30 + name.length + data.length;
    const end = new DataView(new ArrayBuffer(22));
    end.setUint32(0, 0x06054b50, true); end.setUint16(8, 1, true); end.setUint16(10, 1, true);
    end.setUint32(12, 46 + name.length, true); end.setUint32(16, localSize, true);
    return new Blob([local, name, data, central, name, end]);
  }

  const hostDownloads = window.claude && typeof window.claude.use === 'function'
    ? window.claude.use('downloads').catch(() => null)
    : null;

  async function saveSbi() {
    const msg = $('sbiMsg');
    msg.classList.remove('err');
    const base = pyName(patch.name);
    const data = bytesToSbi(patch.name, gameBytes(patch));
    if (hostDownloads) {
      // The hosted page can only hand out a few file types, so the SBI goes in a zip.
      const dl = await hostDownloads;
      if (!dl) {
        msg.classList.add('err');
        msg.textContent = 'Saving files is not available in this view. Copy the hex line instead.';
        return;
      }
      try {
        await dl.save({ filename: `${base}.zip`, data: zipOne(`${base}.sbi`, data) });
        msg.textContent = `Saved ${base}.zip with ${base}.sbi inside.`;
      } catch (e) {
        if (e && e.code === 'declined') msg.textContent = 'Save cancelled.';
        else {
          msg.classList.add('err');
          msg.textContent = `Could not save the file (${(e && (e.message || e.code)) || e}). Copy the hex line instead.`;
        }
      }
      return;
    }
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([data], { type: 'application/octet-stream' }));
    a.download = `${base}.sbi`;
    document.body.appendChild(a);
    a.click();
    a.remove();
    setTimeout(() => URL.revokeObjectURL(a.href), 5000);
    msg.textContent = `Saved ${base}.sbi.`;
  }

  async function importSbi(files) {
    const msg = $('sbiMsg');
    const done = [];
    const failed = [];
    for (const file of files) {
      try {
        const { name, bytes } = sbiToBytes(await file.arrayBuffer());
        done.push({ name: name || file.name.replace(/\.[^.]*$/, ''), bytes });
      } catch (e) {
        failed.push(`${file.name}: ${e.message}`);
      }
    }
    done.forEach((d) => addMine(patchFromBytes(d.name, d.bytes)));
    if (done.length) loadGameBytes(done[0].name, done[0].bytes);
    msg.classList.toggle('err', failed.length > 0);
    msg.textContent = [
      done.length ? `Imported ${done.map((d) => d.name).join(', ')}.` : '',
      failed.length ? `Skipped ${failed.join('; ')}.` : '',
    ].join(' ').trim();
  }

  // ------------------------------------------------------- my patches
  let mine = [];
  let storageOk = true;

  function readStore(key) {
    try {
      const raw = window.localStorage.getItem(key);
      return raw ? JSON.parse(raw) : null;
    } catch (e) {
      storageOk = false;
      return null;
    }
  }

  function writeStore(key, value) {
    try {
      window.localStorage.setItem(key, JSON.stringify(value));
    } catch (e) {
      storageOk = false;
    }
  }

  let saveTimer = 0;
  function saveCurrent() {
    clearTimeout(saveTimer);
    saveTimer = setTimeout(() => writeStore(STORE_CURRENT, { patch, loadedFrom }), 400);
  }

  function addMine(p) {
    mine.push(clone(p));
    writeStore(STORE_PATCHES, mine);
    renderMine();
  }

  function renderMine() {
    const list = $('mine');
    list.textContent = '';
    mine.forEach((p, i) => {
      const li = document.createElement('li');
      const name = document.createElement('span');
      name.className = 'name';
      name.textContent = p.name || `patch ${i + 1}`;
      const load = document.createElement('button');
      load.type = 'button';
      load.textContent = 'Load';
      load.setAttribute('aria-label', `Load ${name.textContent}`);
      load.addEventListener('click', () => loadPatch(p, `mine:${i}`));
      const del = document.createElement('button');
      del.type = 'button';
      del.className = 'danger';
      del.textContent = 'Delete';
      del.setAttribute('aria-label', `Delete ${name.textContent}`);
      let armed = false;
      del.addEventListener('click', () => {
        if (!armed) {
          armed = true;
          del.textContent = 'Confirm delete';
          setTimeout(() => { armed = false; del.textContent = 'Delete'; }, 4000);
          return;
        }
        mine.splice(i, 1);
        writeStore(STORE_PATCHES, mine);
        renderMine();
      });
      const hex = document.createElement('span');
      hex.className = 'hex';
      hex.textContent = gameBytes(p).map(hex2).join(' ') + (p.x && p.x.opl3 ? ' · OPL3 extras' : '');
      li.append(name, load, del, hex);
      list.appendChild(li);
    });
    $('mineEmpty').hidden = mine.length > 0;
    if (!storageOk) {
      $('mineEmpty').hidden = false;
      $('mineEmpty').textContent = 'This browser is not keeping saved data here, so saved patches last only until you close the page.';
    }
  }

  // --------------------------------------------------------- keyboards
  const LOWER = ['KeyZ', 'KeyS', 'KeyX', 'KeyD', 'KeyC', 'KeyV', 'KeyG', 'KeyB', 'KeyH', 'KeyN', 'KeyJ', 'KeyM', 'Comma', 'KeyL', 'Period', 'Semicolon', 'Slash'];
  const UPPER = ['KeyQ', 'Digit2', 'KeyW', 'Digit3', 'KeyE', 'KeyR', 'Digit5', 'KeyT', 'Digit6', 'KeyY', 'Digit7', 'KeyU', 'KeyI', 'Digit9', 'KeyO', 'Digit0', 'KeyP'];
  const LOWER_CAP = 'Z S X D C V G B H N J M , L . ; /'.split(' ');
  const UPPER_CAP = 'Q 2 W 3 E R 5 T 6 Y 7 U I 9 O 0 P'.split(' ');
  const NOTE_NAMES = ['C', 'C♯', 'D', 'D♯', 'E', 'F', 'F♯', 'G', 'G♯', 'A', 'A♯', 'B'];
  const held = new Set();
  const keyNotes = new Map();

  function keyOffset(code) {
    let i = UPPER.indexOf(code);
    if (i >= 0) return 12 + i;
    i = LOWER.indexOf(code);
    return i >= 0 ? i : -1;
  }

  const baseMidi = () => (octave + 1) * 12;

  function setOctave(o) {
    octave = Math.max(0, Math.min(6, o));
    $('octOut').textContent = `C${octave}`;
    buildPiano();
  }

  function isTextField(el) {
    if (!el || el === document.body) return false;
    if (el.isContentEditable) return true;
    const tag = el.tagName;
    if (tag === 'TEXTAREA' || tag === 'SELECT') return true;
    if (tag === 'INPUT') return !['range', 'checkbox', 'radio', 'button', 'submit', 'file'].includes(el.type);
    return false;
  }

  // Controls that use Space, Enter or Page keys themselves.
  function usesKeys(el) {
    if (!el || el === document.body) return false;
    return ['INPUT', 'BUTTON', 'SUMMARY', 'A', 'LABEL'].includes(el.tagName);
  }

  document.addEventListener('keydown', (e) => {
    if (e.ctrlKey || e.metaKey || e.altKey) return;
    const target = e.target;
    if (isTextField(target)) {
      if (target.id === 'pasteHex' && e.key === 'Enter') loadPasted();
      return;
    }
    const off = keyOffset(e.code);
    if (off >= 0) {
      e.preventDefault();
      if (e.repeat || held.has('k:' + e.code)) return;
      const midi = baseMidi() + off;
      held.add('k:' + e.code);
      keyNotes.set(e.code, midi);
      noteOn(midi, velocity, 'k:' + e.code);
      return;
    }
    if (e.code === 'Minus' || e.code === 'Equal') {
      e.preventDefault();
      setOctave(octave + (e.code === 'Equal' ? 1 : -1));
      return;
    }
    if (e.key === 'Escape') {
      panic();
      return;
    }
    if (usesKeys(target)) return;
    if (e.code === 'PageUp' || e.code === 'PageDown') {
      e.preventDefault();
      setOctave(octave + (e.code === 'PageUp' ? 1 : -1));
    } else if (e.code === 'Space') {
      e.preventDefault();
      if (!e.repeat) setSustain(!sustain);
    }
  });

  document.addEventListener('keyup', (e) => {
    const src = 'k:' + e.code;
    if (!held.has(src)) return;
    held.delete(src);
    pianoMark(keyNotes.get(e.code), false);
    keyNotes.delete(e.code);
    noteOffSrc(src);
  });

  window.addEventListener('blur', () => {
    [...held].forEach((src) => {
      if (!src.startsWith('k:')) return;
      held.delete(src);
      const code = src.slice(2);
      pianoMark(keyNotes.get(code), false);
      keyNotes.delete(code);
      noteOffSrc(src);
    });
  });

  // On-screen piano.
  const pointerNotes = new Map();

  function pianoKeyCount() {
    return window.innerWidth < 600 ? 17 : 29;
  }

  function buildPiano() {
    const piano = $('piano');
    piano.textContent = '';
    const count = pianoKeyCount();
    const base = baseMidi();
    const isBlack = (m) => [1, 3, 6, 8, 10].includes(m % 12);
    const whites = [];
    for (let i = 0; i < count; i++) if (!isBlack(base + i)) whites.push(i);
    const ww = 100 / whites.length;
    let whiteIndex = 0;
    for (let i = 0; i < count; i++) {
      const midi = base + i;
      const key = document.createElement('div');
      key.dataset.midi = midi;
      const cap = i < 12 ? LOWER_CAP[i] : UPPER_CAP[i - 12];
      key.setAttribute('aria-label', `${NOTE_NAMES[midi % 12]}${Math.floor(midi / 12) - 1}`);
      if (isBlack(midi)) {
        key.className = 'black';
        key.style.left = `calc(${whiteIndex * ww}% - ${ww * 0.3}%)`;
        key.style.width = `${ww * 0.6}%`;
      } else {
        key.className = 'white';
        whiteIndex++;
        if (midi % 12 === 0) {
          const c = document.createElement('span');
          c.className = 'c-label';
          c.textContent = `C${Math.floor(midi / 12) - 1}`;
          key.appendChild(c);
        }
      }
      key.appendChild(document.createTextNode(cap));
      piano.appendChild(key);
    }
    voices.forEach((v) => { if (v.keyed && !v.sustained) pianoMark(v.note, true); });
  }

  function pianoMark(midi, on) {
    if (midi === undefined) return;
    const key = document.querySelector(`.piano [data-midi="${midi}"]`);
    if (key) key.classList.toggle('down', on);
  }

  function keyAt(x, y) {
    const el = document.elementFromPoint(x, y);
    const key = el && el.closest('.piano [data-midi]');
    return key ? Number(key.dataset.midi) : null;
  }

  function pointerPlay(e) {
    const midi = keyAt(e.clientX, e.clientY);
    const src = 'p:' + e.pointerId;
    const prev = pointerNotes.get(e.pointerId);
    if (midi === prev) return;
    if (prev !== undefined && prev !== null) {
      pianoMark(prev, false);
      held.delete(src);
      noteOffSrc(src);
    }
    pointerNotes.set(e.pointerId, midi);
    if (midi !== null) {
      held.add(src);
      noteOn(midi, velocity, src);
    }
  }

  function pointerStop(e) {
    if (!pointerNotes.has(e.pointerId)) return;
    const prev = pointerNotes.get(e.pointerId);
    pointerNotes.delete(e.pointerId);
    const src = 'p:' + e.pointerId;
    held.delete(src);
    if (prev !== null) pianoMark(prev, false);
    noteOffSrc(src);
  }

  function bindPiano() {
    const piano = $('piano');
    piano.addEventListener('pointerdown', (e) => {
      e.preventDefault();
      if (piano.releasePointerCapture && piano.hasPointerCapture && piano.hasPointerCapture(e.pointerId)) piano.releasePointerCapture(e.pointerId);
      pointerPlay(e);
    });
    window.addEventListener('pointermove', (e) => { if (pointerNotes.has(e.pointerId)) pointerPlay(e); });
    window.addEventListener('pointerup', pointerStop);
    window.addEventListener('pointercancel', pointerStop);
    let resizeTimer = 0;
    let lastCount = pianoKeyCount();
    window.addEventListener('resize', () => {
      clearTimeout(resizeTimer);
      resizeTimer = setTimeout(() => {
        if (pianoKeyCount() !== lastCount) { lastCount = pianoKeyCount(); buildPiano(); }
        for (let i = 0; i < 4; i++) drawEnvelope(i);
      }, 150);
    });
  }

  // ------------------------------------------------------------- MIDI
  let midiAccess = null;

  function bindMidiInput(input) {
    if (!midiAccess) return;
    midiAccess.inputs.forEach((inp) => { inp.onmidimessage = null; });
    if (!input) return;
    input.onmidimessage = (ev) => {
      const [status, a, b] = ev.data;
      const kind = status & 0xF0;
      const ch = status & 0x0F;
      const src = `m:${ch}:${a}`;
      if (kind === 0x90 && b > 0) {
        held.add(src);
        noteOn(a, b, src);
      } else if (kind === 0x80 || (kind === 0x90 && b === 0)) {
        held.delete(src);
        pianoMark(a, false);
        noteOffSrc(src);
      } else if (kind === 0xB0 && a === 64) {
        setSustain(b >= 64);
      } else if (kind === 0xB0 && (a === 120 || a === 123)) {
        panic();
      }
    };
  }

  async function connectMidi() {
    const msg = $('midiMsg');
    msg.classList.remove('err');
    if (!navigator.requestMIDIAccess) {
      msg.classList.add('err');
      msg.textContent = 'This browser has no Web MIDI. Chrome and Edge support it; Firefox asks for an add-on.';
      return;
    }
    try {
      midiAccess = await navigator.requestMIDIAccess();
    } catch (e) {
      msg.classList.add('err');
      msg.textContent = 'MIDI access was refused here. Embedded pages often cannot use MIDI; open the lab file from tools/fm_lab/ in Chrome or Edge instead.';
      return;
    }
    const sel = $('midiIn');
    const fill = () => {
      const inputs = [...midiAccess.inputs.values()];
      sel.textContent = '';
      inputs.forEach((inp) => {
        const o = document.createElement('option');
        o.value = inp.id;
        o.textContent = inp.name || inp.id;
        sel.appendChild(o);
      });
      sel.hidden = inputs.length === 0;
      msg.textContent = inputs.length ? `Listening to ${inputs[0].name || 'MIDI input'}.` : 'MIDI is on, but no input is connected. Plug one in and it will appear here.';
      bindMidiInput(inputs[0] || null);
    };
    sel.onchange = () => {
      const inp = midiAccess.inputs.get(sel.value);
      bindMidiInput(inp);
      msg.textContent = `Listening to ${inp ? inp.name : 'nothing'}.`;
    };
    midiAccess.onstatechange = fill;
    fill();
  }

  // ------------------------------------------------------------- wiring
  function init() {
    buildPresets();
    buildChannel();
    [0, 1].forEach((i) => buildOperator($('ops'), i, () => patch.ops[i], [0, 1, 2, 3]));
    buildExtras();

    mine = readStore(STORE_PATCHES) || [];
    if (!Array.isArray(mine)) mine = [];
    const saved = readStore(STORE_CURRENT);
    if (saved && saved.patch && saved.patch.ops) {
      patch = saved.patch;
      if (!patch.x) patch.x = defaultExtras();
      loadedFrom = saved.loadedFrom || loadedFrom;
    }
    $('patchName').value = patch.name || '';

    buildVoices();
    refreshControls();
    renderHex();
    renderAlgo();
    renderEdited();
    renderPresetButtons();
    renderMine();
    for (let i = 0; i < 4; i++) drawEnvelope(i);
    buildPiano();
    bindPiano();

    $('start').addEventListener('click', () => startAudio());
    $('panic').addEventListener('click', panic);
    $('panic2').addEventListener('click', panic);
    $('sustain').addEventListener('click', () => setSustain(!sustain));
    $('octDown').addEventListener('click', () => setOctave(octave - 1));
    $('octUp').addEventListener('click', () => setOctave(octave + 1));
    const showVel = () => {
      const att = attenuation(velocity);
      $('velOut').textContent = `${velocity} · −${(att * 0.75).toFixed(2).replace(/\.?0+$/, '')} dB`;
      $('velocity').setAttribute('aria-valuetext', `${velocity}, carrier ${att} steps quieter`);
    };
    $('velocity').addEventListener('input', () => { velocity = Number($('velocity').value); showVel(); });
    showVel();
    const showMaster = () => {
      $('masterOut').textContent = `${master}%`;
      if (node) node.port.postMessage({ gain: gainValue() });
    };
    $('master').addEventListener('input', () => { master = Number($('master').value); showMaster(); });
    showMaster();
    $('copyHex').addEventListener('click', (e) => copyText($('hexLine').textContent.replace(/\//g, ' / ').replace(/\s+/g, ' '), e.currentTarget));
    $('copyPy').addEventListener('click', (e) => copyText(pythonRow(), e.currentTarget));
    $('loadHex').addEventListener('click', loadPasted);
    $('patchName').addEventListener('input', () => { patch.name = $('patchName').value.trim(); saveCurrent(); });
    $('savePatch').addEventListener('click', () => {
      const name = $('patchName').value.trim() || `patch ${mine.length + 1}`;
      patch.name = name;
      addMine(patch);
      loadedFrom = { name: `mine:${mine.length - 1}`, bytes: JSON.stringify([gameBytes(patch), patch.x]) };
      renderEdited();
      renderPresetButtons();
    });
    $('sbiFile').addEventListener('change', () => {
      const files = [...$('sbiFile').files];
      if (files.length) importSbi(files);
      $('sbiFile').value = '';
    });
    $('sbiSave').addEventListener('click', saveSbi);
    $('midiBtn').addEventListener('click', connectMidi);
    $('drawer').addEventListener('toggle', () => { drawEnvelope(2); drawEnvelope(3); });
    if (window.matchMedia) {
      window.matchMedia('(prefers-color-scheme: dark)').addEventListener('change', () => { for (let i = 0; i < 4; i++) drawEnvelope(i); });
    }
    new MutationObserver(() => { for (let i = 0; i < 4; i++) drawEnvelope(i); })
      .observe(document.documentElement, { attributes: true, attributeFilter: ['data-theme'] });

    // Test hooks for automated checks; not used by the page itself.
    window.fmLab = { parseHex, gameBytes: () => gameBytes(patch), sbiToBytes, bytesToSbi, pythonRow, loadGameBytes, noteOn, noteOffSrc, shadow, voices: () => voices, scope: () => latestScope, startAudio, panic };
  }

  init();
})();
