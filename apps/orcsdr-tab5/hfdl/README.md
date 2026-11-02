# HFDL decoder (ESP32-P4)

End-to-end path on branch `chatgpt/hfdl-dashboard-decoder`:

```
audio/IQ samples
  → hfdl::Decoder::process()
  → hfdl_ui::Model::post_message()   (lock-free SPSC)
  → Model::service() / Snapshot
  → hfdl_dashboard M5GFX list
```

## What is real (from REA-Results notes)
- 1800 symbols/s M-PSK
- Carrier offset +1440 Hz
- 32 s frames, 13 slots, slot 1 = SPDU squitter
- USB, 2.8–22 MHz

## What is still placeholder
- Actual PSK demod, preamble detection, deinterleave, FEC
- Exact SPDU / MPDU bit layouts (need Doc 9741 or ARINC 635)
- Slot timing vs UTC

## Memory budget (current)
- Message ring: 24 × ~200 B ≈ 5 KB
- Queue: 8 slots
- No heap after init

Next concrete step: replace the frame-timer placeholder in `hfdl_decoder.cpp` with a real 1800 Bd correlator once a short IQ capture is available.
