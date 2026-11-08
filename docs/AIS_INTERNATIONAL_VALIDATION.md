# AIS international NMEA validation and DAC/FI coverage

**Status: development, no hardware validation.** This is independently sourced NMEA (not IQ or RF sensitivity proof).

## Sources and use restrictions

- Worldwide AIS Hub NMEA corpus dated November 9, 2025: https://github.com/Dirk--/AIS-testing
  Pinned to GitHub commit 49b9d6b15d348914d99983a720ed21cabec2abcb.
  Includes message types 6, 8, 12, 25, 26. The manifest lists the exact Git blob hashes and source row counts. An explicit opt-in fetch downloads source files to a *local test cache*, not this Git repository. No third-party bulk dataset is bundled into OrcSDR. Upstream redistribution permissions are not verified.
- Japanese research examples: https://j-nav.org/mte/pdf/201307.pdf (Japan National Maritime Research Institute, 2013, page 7). A small NMEA excerpt is in tests/fixtures/ais_international/japan_research_nmea.tsv. Some transcribed/OCR strings have invalid NMEA checksums: these are negative tests and must not be silently repaired.
- Global Fishing Watch: https://globalfishingwatch.org/platform-update/global-ais-vessel-presence-dataset/
  Dataset ID public-global-presence:latest. **Reference for geographic map density only**, not an IQ/NMEA protocol oracle. Its data is gridded hourly vessel presence, not individual vessel tracks. General site/data terms are noncommercial CC BY-NC: https://globalfishingwatch.org/terms-of-use/. No GFW data is downloaded or shipped here. Commercial use requires separate permission.
- International specifications: ITU-R M.1371-6 (Feb 2026); IALA ASM registry entries https://academy.iala.int/asm/number-persons-board-3/ , https://www.iala.int/asm/number-persons-board/ , https://www.iala.int/asm/inland-ship-static-voyage-related-data/.

## What is implemented

Portable original C++17 ais_binary_app (no heap, no UI or SDR dependencies) handles the envelopes of messages 6, 8, 25, 26. It reads DAC (10 bits) and FI (6 bits), with message type forming part of the registry key. Unknown combinations remain intact as MSB-first raw application bits. Unstructured Type 25/26 payloads remain unstructured. Type 26 radio communication state is excluded from the application payload.

Only these three *documented* applications are field-decoded:
- Message 6, DAC 1, FI 16: IMO persons aboard, including unknown and 8191-or-more values.
- Message 6, DAC 200, FI 55: EU inland crew, passengers and onboard personnel, including their unknown sentinels.
- Message 8, DAC 200, FI 10: EU inland vessel identification, size, vessel type, cargo, draught and information-quality flags.

Russian-associated DAC 1 FI 29/30 entries and every other unimplemented regional message are preserved, **not misidentified as decoded**. No country-specific radio receiver is needed for standard AIS messages.

The existing ais_rx::Pipeline calls the application parser for CRC-valid binary messages and optionally invokes a nonblocking BinaryObserver. It never changes or takes ownership of the radio or M5GFX. The other AIS message types still have partial field coverage.

## Independent validation

tools/ais/validate_nmea_corpus.py checks published NMEA sentence XOR checksums, six-bit armoring, payload padding, fragment completion, source identity, message type, and published DAC/FI metadata. It exports complete bit-precise AIS payloads as TSV, reporting missing fragments rather than guessing their contents. tests/ais_binary_corpus_runner.cpp exercises the *actual C++ decoder* against those independently sourced bytes. Host synthetic tests in tests/ais_binary_app_tests.cpp verify the three known IALA field layouts and bounds/sentinels.

**NMEA checksum validation does not imply an RF CRC, live reception or verified decoding of every binary application field.**

## Run/test from repository root on Linux or WSL

Create an output directory; every command pipes its result to a log in that directory:

~~~bash
mkdir -p ais-test-runs
python3 tools/ais/validate_nmea_corpus.py --japan-only --export ais-test-runs/japan.tsv 2>&1 | tee ais-test-runs/japan.log

g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iapps/orcsdr-tab5/ui apps/orcsdr-tab5/ui/ais_binary_app.cpp tests/ais_binary_app_tests.cpp -o ais-test-runs/ais-binary-tests
./ais-test-runs/ais-binary-tests 2>&1 | tee ais-test-runs/binary-tests.log

g++ -std=c++17 -O1 -g -Wall -Wextra -Wpedantic -Werror -fsanitize=address,undefined -fno-omit-frame-pointer -Iapps/orcsdr-tab5/ui apps/orcsdr-tab5/ui/ais_binary_app.cpp apps/orcsdr-tab5/ui/ais_protocol.cpp tests/ais_binary_corpus_runner.cpp -o ais-test-runs/ais-corpus-runner
./ais-test-runs/ais-corpus-runner ais-test-runs/japan.tsv 4 2>&1 | tee ais-test-runs/japan-decoder.log

# Explicit opt-in: fetch pinned public external source to local test cache only.
python3 tools/ais/validate_nmea_corpus.py --fetch --cache-dir ais-test-runs/corpus --export ais-test-runs/worldwide.tsv 2>&1 | tee ais-test-runs/worldwide.log
./ais-test-runs/ais-corpus-runner ais-test-runs/worldwide.tsv 2>&1 | tee ais-test-runs/worldwide-decoder.log
~~~

To rerun offline, omit --fetch and use the already downloaded cache. Blob SHA-1 is rechecked each time. CI runs the same tests on the branch.

## Not yet done

No real Japanese or Russian RF IQ captures, no full country-specific binary field catalogue, no independent field-by-field oracle for every DAC/FI, no complete Type 1-28 semantic parsing, no ESP32-P4 hardware profiling, and no OrcMaps integration or application UI journal for binary messages. Maintain clear provenance and version distinctions before adding more registry entries.
