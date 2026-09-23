# Licensing

OrcSDR is dual-licensed under the same model as OrcLink:

1. **AGPL-3.0-only** (default) — see [LICENSE](LICENSE).
2. **Commercial** terms available by agreement — contact hardcoreerik@gmail.com.

The RTL-SDR driver is the independent
[`hardcoreerik/esp-rtl-sdr`](https://github.com/hardcoreerik/esp-rtl-sdr)
project, consumed here as a version-pinned dependency. It currently uses the
same AGPL-3.0-only plus commercial-license model, but has its own repository,
license files, versions, and release train. For proprietary driver use, follow
that project's licensing terms rather than treating it as in-tree OrcSDR code.

The FLARM packet decoder in `flarm_decoder_core.cpp` incorporates SoftRF code
under **GPL-3.0-or-later**, with its original copyright notices retained. These
third-party contributions are not covered by OrcSDR's offer of a separate
commercial license. See [FLARM source provenance](docs/FLARM.md#source-provenance).
