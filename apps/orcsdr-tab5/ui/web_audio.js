/* OrcSDR PCM transport/playback. Plain HTTP; no AudioWorklet dependency. */
(function (root) {
  'use strict';

  function parsePacket(bytes) {
    if (!(bytes instanceof ArrayBuffer) || bytes.byteLength < 32 || bytes.byteLength > 1952)
      throw new Error('Invalid audio packet length');
    const v = new DataView(bytes);
    if (v.getUint32(0, true) !== 0x4143524f || v.getUint16(4, true) !== 1 ||
        v.getUint16(6, true) !== 32 || v.getUint32(20, true) !== 48000 ||
        v.getUint8(26) !== 1 || v.getUint8(27) !== 1 || v.getUint32(28, true) > 1)
      throw new Error('Unsupported audio packet format');
    const frames = v.getUint16(24, true);
    const position = v.getUint32(12, true) + v.getUint32(16, true) * 4294967296;
    if (frames < 1 || frames > 960 || bytes.byteLength !== 32 + frames * 2 ||
        !Number.isSafeInteger(position + frames))
      throw new Error('Invalid audio sample range');
    const samples = new Float32Array(frames);
    for (let i = 0; i < frames; i++) samples[i] = v.getInt16(32 + i * 2, true) / 32768;
    return { generation: v.getUint32(8, true), position, rate: 48000,
      discontinuity: v.getUint32(28, true) === 1, samples };
  }

  class Player {
    constructor(context) {
      this.context = context;
      this.gain = context.createGain();
      this.gain.connect(context.destination);
      this.state = 'buffering';
      this.stats = { underruns: 0, overflows: 0, gaps: 0, stale: 0 };
      this.queue = [];
      this.queuedSeconds = 0;
      this.nodes = [];
      this.end = context.currentTime;
      this.start = Infinity;
      this.generation = null;
      this.expected = null;
      this.running = false;
    }

    get bufferSeconds() {
      return this.queuedSeconds + Math.max(0, this.end - this.context.currentTime);
    }
    get nodeCount() { return this.queue.length + this.nodes.length; }

    clear() {
      const now = this.context.currentTime;
      this.gain.gain.cancelScheduledValues(now);
      this.gain.gain.setValueAtTime(this.gain.gain.value, now);
      this.gain.gain.linearRampToValueAtTime(0, now + 0.005);
      for (const item of this.nodes) {
        item.node.onended = () => item.node.disconnect();
        item.node.stop(now + 0.005);
      }
      this.nodes = [];
      this.queue = [];
      this.queuedSeconds = 0;
      this.end = now;
      this.start = Infinity;
      this.running = false;
      if (this.state !== 'off') this.state = 'buffering';
    }

    stop() {
      this.state = 'off';
      this.clear();
      this.gain.disconnect();
    }

    tick() {
      if (this.state === 'off') return;
      const now = this.context.currentTime;
      this.nodes = this.nodes.filter(item => {
        if (item.end > now) return true;
        item.node.disconnect();
        return false;
      });
      if (this.context.state !== 'running') {
        this.clear();
        this.state = 'suspended';
        return;
      }
      if (this.running && now >= this.end) {
        this.stats.underruns++;
        this.clear();
      } else if (this.running && now >= this.start) this.state = 'live';
    }

    push(bytes) {
      if (this.state === 'off') return;
      const packet = parsePacket(bytes);
      this.tick();
      if (this.context.state !== 'running') return;
      if (this.state === 'suspended') this.state = 'buffering';
      if (this.generation !== null && packet.generation !== this.generation) {
        // uint32 generation ordering, including wrap; reconnect creates a new Player.
        if (((packet.generation - this.generation) | 0) <= 0) {
          this.stats.stale++;
          return;
        }
        this.clear();
        this.expected = null;
      }
      this.generation = packet.generation;
      if (this.expected !== null && packet.position < this.expected) {
        this.stats.stale++;
        return;
      }
      if (packet.discontinuity || (this.expected !== null && packet.position !== this.expected)) {
        this.stats.gaps++;
        this.clear();
      }
      this.expected = packet.position + packet.samples.length;
      const duration = packet.samples.length / packet.rate;
      if (this.bufferSeconds + duration > 0.6 || this.nodeCount >= 64) {
        this.stats.overflows++;
        this.clear();
      }
      this.queue.push(packet.samples);
      this.queuedSeconds += duration;
      if (!this.running && this.queuedSeconds < 0.3 - 1e-9) return;
      const now = this.context.currentTime;
      if (!this.running) {
        this.start = this.end = now + 0.02;
        this.running = true;
        this.gain.gain.cancelScheduledValues(this.start);
        this.gain.gain.setValueAtTime(0, this.start);
        this.gain.gain.linearRampToValueAtTime(1, this.start + 0.005);
      }
      // Bounded correction follows buffered duration, not arrival packet intervals.
      // +/-0.5% is a recovery ceiling, not a promise of calibrated audio pitch.
      const rate = Math.max(0.995, Math.min(1.005,
        1 + (this.bufferSeconds - 0.3) * 0.01));
      for (const samples of this.queue) {
        const buffer = this.context.createBuffer(1, samples.length, 48000);
        buffer.getChannelData(0).set(samples);
        const node = this.context.createBufferSource();
        node.buffer = buffer;
        node.playbackRate.value = rate;
        node.connect(this.gain);
        node.onended = () => node.disconnect();
        node.start(this.end);
        this.end += samples.length / 48000 / rate;
        this.nodes.push({ node, end: this.end });
      }
      this.queue = [];
      this.queuedSeconds = 0;
    }
  }

  class Connection {
    constructor(context, onState, environment = root) {
      this.context = context;
      this.onState = onState;
      this.env = environment;
      this.run = 0;
      this.socket = null;
      this.player = null;
      this.timer = this.interval = null;
      this.state = 'off';
      this.retries = 0;
    }
    setState(state) {
      if (this.state === state) return;
      this.state = state;
      this.onState(state);
    }
    cleanup() {
      if (this.timer !== null) this.env.clearTimeout(this.timer);
      if (this.interval !== null) this.env.clearInterval(this.interval);
      this.timer = this.interval = null;
      const old = this.socket;
      this.socket = null;
      if (old) old.close();
      if (this.player) this.player.stop();
      this.player = null;
    }
    stop() {
      ++this.run;
      this.cleanup();
      this.setState('off');
    }
    start(url) {
      this.stop();
      this.url = url;
      this.retries = 0;
      this.connect(this.run);
    }
    connect(run) {
      if (run !== this.run) return;
      this.timer = null;
      this.setState(this.retries ? 'reconnecting' : 'connecting');
      let socket;
      try { socket = new this.env.WebSocket(this.url); }
      catch (_) { this.setState('error'); return; }
      this.socket = socket;
      socket.binaryType = 'arraybuffer';
      const current = () => this.run === run && this.socket === socket;
      const reconnect = () => {
        if (!current()) return;
        this.cleanup();
        this.setState('reconnecting');
        const delay = Math.min(5000, 500 * Math.pow(2, Math.min(this.retries++, 4)));
        this.timer = this.env.setTimeout(() => this.connect(run), delay);
      };
      this.timer = this.env.setTimeout(reconnect, 5000);
      socket.onopen = () => {
        if (!current()) return;
        this.env.clearTimeout(this.timer);
        this.timer = null;
        this.player = new Player(this.context);
        this.setState('buffering');
        this.interval = this.env.setInterval(() => {
          if (!current()) return;
          this.player.tick();
          if (this.player.state === 'suspended') {
            this.cleanup();
            this.setState('error'); // Requires a new user gesture, not autoplay retries.
          } else this.setState(this.player.state);
        }, 25);
      };
      socket.onmessage = event => {
        if (!current() || !this.player) return;
        try {
          this.player.push(event.data);
          this.retries = 0;
          this.setState(this.player.state);
        } catch (_) {
          this.cleanup();
          this.setState('error');
        }
      };
      socket.onerror = reconnect;
      socket.onclose = reconnect;
    }
  }

  const api = { parsePacket, Player, Connection };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.OrcAudio = api;
})(typeof globalThis !== 'undefined' ? globalThis : this);
