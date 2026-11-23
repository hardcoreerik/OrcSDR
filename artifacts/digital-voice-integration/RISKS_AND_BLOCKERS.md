# Risks and blockers (open)

1. **Radio Owner enum has no Digital Voice entry.** `radio::Owner/Band` (radio_session.hpp) need an
   additive value or DV must ride an existing owner. Needs a decision before G3 (changes receiver arbitration).
2. **main.cpp is 21k lines** and owns scan_engine + dashboard switches. Wiring must be thin hooks calling a new
   `digital_voice_app` module (owner rule: do not grow main.cpp).
3. **Tab5 internal RAM boot pool is nearly full** (memory note: +800 B statics boot-looped one unit). DMR/NXDN
   decoder state and any queues must live in PSRAM; measure before flashing.
4. **Sample rate gate.** DMR and NXDN validated at 48 kS/s only; the RTL path rate/channelizer for DV is undefined.
5. **Scan engine is frequency iteration only** (retune/measure/finish). It has no hold, skip or detection; a DV
   scanner state machine must wrap it.
6. **No hardware actions taken.** Flashing, SD writes and screenshots need the owner to name the board/port first.
7. Pinned real-IQ replay gates and P25 core host tests not yet rerun locally.
8. Voice: AMBE+2 licensing not started; nothing audible is claimed.
