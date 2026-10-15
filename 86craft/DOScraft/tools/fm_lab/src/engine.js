// Runs one emulated chip: the Nuked OPL3 WebAssembly module, or, where a page's
// security policy refuses WebAssembly, the same module translated to
// JavaScript by Binaryen's wasm2js (oplAsmFunc; bit-identical output).
// Shared by the AudioWorklet and the page's main-thread fallback.
class OplEngine {
  constructor(wasmBytes, rate, post, forceJs) {
    this.rate = rate;
    this.post = post;
    this.chip = null;
    this.pending = [];
    this.gain = 0.5;
    this.scope = new Float32Array(2048);
    this.scopeAt = 0;
    // Deferred so the caller can attach its message handler first.
    const useJs = (why) => Promise.resolve().then(() => this.start(oplAsmFunc({}), 'JavaScript (wasm2js)', why));
    if (forceJs) {
      useJs('forced');
      return;
    }
    try {
      WebAssembly.instantiate(wasmBytes, {}).then(
        ({ instance }) => this.start(instance.exports, 'WebAssembly'),
        (error) => useJs(String(error)),
      );
    } catch (error) {
      useJs(String(error));
    }
  }

  start(exports, engine, why) {
    if (exports._initialize) exports._initialize();
    exports.opl_reset(this.rate);
    this.chip = exports;
    const queued = this.pending;
    this.pending = [];
    queued.forEach((msg) => this.receive(msg));
    this.post({ ready: true, sampleRate: this.rate, engine, why });
  }

  receive(msg) {
    if (!this.chip) {
      this.pending.push(msg);
      return;
    }
    if (msg.reset) this.chip.opl_reset(this.rate);
    if (msg.gain !== undefined) this.gain = msg.gain;
    if (msg.writes) {
      const w = msg.writes;
      for (let i = 0; i < w.length; i += 2) this.chip.opl_write(w[i], w[i + 1]);
    }
  }

  render(left, right) {
    const n = left.length;
    if (!this.chip) {
      left.fill(0);
      right.fill(0);
      return;
    }
    const ptr = this.chip.opl_render(n);
    const pcm = new Int16Array(this.chip.memory.buffer, ptr, n * 2);
    const g = this.gain / 32768;
    for (let i = 0; i < n; i++) {
      left[i] = pcm[i * 2] * g;
      right[i] = pcm[i * 2 + 1] * g;
    }
    // Oscilloscope feed: about 41 ms of mono output at a time.
    for (let i = 0; i < n && this.scopeAt < this.scope.length; i++) {
      this.scope[this.scopeAt++] = (pcm[i * 2] + pcm[i * 2 + 1]) / 65536;
    }
    if (this.scopeAt === this.scope.length) {
      this.post({ scope: this.scope }, [this.scope.buffer]);
      this.scope = new Float32Array(2048);
      this.scopeAt = 0;
    }
  }
}
