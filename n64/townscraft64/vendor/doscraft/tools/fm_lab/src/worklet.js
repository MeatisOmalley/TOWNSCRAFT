// AudioWorklet processor around OplEngine (engine.js, prepended at build time
// together with the wasm2js fallback). The page sends register writes; the chip
// renders at the AudioContext rate, ideally its own 49716 Hz so nothing
// resamples.
class OplProcessor extends AudioWorkletProcessor {
  constructor(options) {
    super();
    this.engine = new OplEngine(options.processorOptions.wasm, sampleRate,
      (msg, transfer) => this.port.postMessage(msg, transfer || []), options.processorOptions.forceJs);
    this.port.onmessage = (event) => this.engine.receive(event.data);
  }

  process(inputs, outputs) {
    const out = outputs[0];
    this.engine.render(out[0], out[1] || new Float32Array(out[0].length));
    return true;
  }
}

registerProcessor('opl-processor', OplProcessor);
