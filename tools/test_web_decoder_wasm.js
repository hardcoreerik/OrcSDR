'use strict';
// ABI smoke check, not a protocol-reception or replay-parity test.
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const output = path.resolve(__dirname, '../apps/orcsdr-web/decoders/build/wasm');
const create = require(path.join(output, 'orcdec.js'));
(async () => {
  const m = await create({wasmBinary: fs.readFileSync(path.join(output, 'orcdec.wasm'))});
  const info = m._malloc(20);
  const expected = [null, ['pocsag', 1, 960000], ['ft8', 2, 12000],
    ['sstv', 2, 48000], ['adsb', 1, 2048000], ['acars', 1, 240000],
    ['p25', 1, 960000], ['js8', 2, 12000], ['lora', 1, 960000]];
  try {
    assert.equal(m._orcdec_info(99, info), -1);
    assert.equal(m._orcdec_create(99, 0), 0);
    for (let kind = 1; kind <= 8; ++kind) {
      assert.equal(m._orcdec_info(kind, info), 0);
      const words = m.HEAPU32.subarray(info >>> 2, (info >>> 2) + 5);
      assert.equal(words[0], kind);
      assert.equal(words[1], expected[kind][1]);
      assert.equal(words[2], expected[kind][2]);
      assert.equal(words[3], 2);
      assert.equal(m.UTF8ToString(words[4]), expected[kind][0]);
      const decoder = m._orcdec_create(kind, 0);
      assert.notEqual(decoder, 0);
      m._orcdec_reset(decoder);
      assert.equal(m._orcdec_dropped(decoder), 0);
      m._orcdec_destroy(decoder);
    }
  } finally { m._free(info); }
  console.log('WEB_DECODER_WASM_ABI_PASS kinds=8 (no RF/replay claim)');
})().catch(error => { console.error(error.message); process.exitCode = 1; });
