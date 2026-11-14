// Shares the app's open companion link: every frame the app sends or receives is seen here, and the MobMesh console sends through the same queue.
(() => {
  const listeners = new Set(), sent = new Set();
  const tap = self.mmTap = {mode: null, loginKey: null, connected: false,
    onFrame(fn) { listeners.add(fn); return () => listeners.delete(fn); },
    onSent(fn) { sent.add(fn); return () => sent.delete(fn); },
    send: null};
  const hex = b => Array.from(b, x => x.toString(16).padStart(2, '0')).join('');
  // Contact records (code 3) the app has read, by full key, so the console can name the repeater without asking again.
  tap.contacts = new Map();
  const rx = frame => { if (frame[0] === 3 && frame.length >= 148) tap.contacts.set(hex(frame.slice(1, 33)), frame); for (const fn of [...listeners]) { try { fn(frame); } catch (e) { console.warn('mmTap listener', e); } } };
  // Login (command 26) carries the repeater's full key: the panel targets whichever repeater the app last logged in to.
  const tx = frame => { if (frame[0] === 26 && frame.length >= 33) tap.loginKey = hex(frame.slice(1, 33)); for (const fn of [...sent]) { try { fn(frame); } catch {} } };

  // One operation at a time; a failed one does not block the next.
  function queued() { let tail = Promise.resolve(); return fn => { const p = tail.then(fn, fn); tail = p.catch(() => {}); return p; }; }
  // USB: frames are '<' or '>' plus a little-endian length.
  function framer(start, onFrame) {
    let buf = [];
    return bytes => {
      buf.push(...bytes);
      while (buf.length >= 3) {
        const n = buf[1] | buf[2] << 8;
        if (buf[0] !== start || !n || n > 300) { buf.shift(); continue; }
        if (buf.length < n + 3) break;
        onFrame(Uint8Array.from(buf.splice(0, n + 3).slice(3)));
      }
    };
  }
  if (self.SerialPort) {
    const P = SerialPort.prototype, readable = Object.getOwnPropertyDescriptor(P, 'readable'), writable = Object.getOwnPropertyDescriptor(P, 'writable');
    P.close = (close => function () { tap.connected = false; tap.send = null; return close.call(this); })(P.close);
    // The app's reader is wrapped so each chunk it reads is also framed here; nothing is read twice.
    Object.defineProperty(P, 'readable', {configurable: true, get() {
      const r = readable.get.call(this);
      if (r && !r.mmTapped) {
        r.mmTapped = true;
        const feed = framer(0x3e, rx), getReader = r.getReader.bind(r);
        r.getReader = (...a) => { const reader = getReader(...a), read = reader.read.bind(reader); reader.read = () => read().then(res => { if (!res.done && res.value) feed(res.value); return res; }); return reader; };
      }
      return r;
    }});
    // The app sees a writer whose writes join one queue; the real writer is held only for each write, so the console can share it.
    Object.defineProperty(P, 'writable', {configurable: true, get() {
      const w = writable.get.call(this);
      if (!w) return w;
      if (this.mmWritable?.real !== w) {
        const feed = framer(0x3c, tx), serial = queued();
        const write = bytes => serial(async () => { const real = w.getWriter(); try { await real.write(bytes); } finally { real.releaseLock(); } });
        const writer = {ready: Promise.resolve(), closed: new Promise(() => {}), desiredSize: 1, releaseLock() {}, close: () => queue, abort: () => queue,
          write(bytes) { const b = Uint8Array.from(bytes); feed(b); return write(b); }};
        this.mmWritable = {real: w, writer: {real: w, locked: false, getWriter: () => writer}};
        tap.mode = 'usb'; tap.connected = true;
        tap.send = payload => { const b = Uint8Array.from([0x3c, payload.length & 255, payload.length >> 8, ...payload]); return write(b); };
      }
      return this.mmWritable.writer;
    }});
  }

  // Bluetooth: one frame per write and per notification; GATT allows one operation at a time, so writes share a queue.
  if (self.BluetoothRemoteGATTCharacteristic) {
    const C = BluetoothRemoteGATTCharacteristic.prototype, gatt = queued(), orig = {};
    for (const name of ['writeValueWithResponse', 'writeValueWithoutResponse']) {
      orig[name] = C[name];
      C[name] = function (value) {
        const b = Uint8Array.from(value instanceof Uint8Array ? value : new Uint8Array(value.buffer || value)), ch = this;
        tx(b);
        tap.mode = 'ble'; tap.connected = true;
        tap.send = payload => gatt(() => orig[name].call(ch, Uint8Array.from(payload)));
        return gatt(() => orig[name].call(ch, b));
      };
    }
    C.startNotifications = (start => function () {
      if (!this.mmTapped) { this.mmTapped = true; this.addEventListener('characteristicvaluechanged', e => { const v = e.target.value; rx(new Uint8Array(v.buffer.slice(v.byteOffset, v.byteOffset + v.byteLength))); }); }
      this.service?.device?.addEventListener('gattserverdisconnected', () => { tap.connected = false; tap.send = null; }, {once: true});
      return start.call(this);
    })(C.startNotifications);
  }
})();
