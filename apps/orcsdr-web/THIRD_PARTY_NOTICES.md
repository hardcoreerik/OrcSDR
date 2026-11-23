# Third-party notices for the OrcSDR web build

`core.wasm` contains code from M5GFX, fetched at build time (see `PROVENANCE.md`). From M5GFX 0.2.25's own README (licence section), which lists
the licences of everything it contains:

| Component | Licence |
|---|---|
| M5GFX | MIT, Copyright (c) 2021 M5Stack |
| LovyanGFX (lovyan03) | FreeBSD |
| Pngle (kikuchan), QRCode (Richard Moore, Nayuki), result (Matthew Rodusek) | MIT |
| GFX font and GLCD font (Adafruit Industries) | 2-clause BSD |
| Font 2, 4, 6, 7, 8 (Bodmer) | FreeBSD |
| Converted IPA font | IPA Font License v1.0 |
| efont (The Electronic Font Open Laboratory) | 3-clause BSD |

M5GFX's MIT licence text, reproduced as required:

```
MIT License

Copyright (c) 2021 M5Stack

Permission is hereby granted, free of charge, to any person obtaining a copy
of this software and associated documentation files (the "Software"), to deal
in the Software without restriction, including without limitation the rights
to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
copies of the Software, and to permit persons to whom the Software is
furnished to do so, subject to the following conditions:

The above copyright notice and this permission notice shall be included in all
copies or substantial portions of the Software.

THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
SOFTWARE.
```

Open: the FreeBSD/BSD texts for LovyanGFX, Bodmer and Adafruit are given by URL inside M5GFX, and the exact copyright lines were not
reproduced here. Before a release that ships `core.wasm`, add those texts (taken from the upstream files) to this notice.
