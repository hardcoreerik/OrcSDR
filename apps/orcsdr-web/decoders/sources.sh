# Sources shared by every build of orcdec (host, WASM, ESP-IDF). Sourced by the build scripts; D must be set.
V=$R/apps/orcsdr-tab5/ui   # the device decoder sources, compiled in place (no copy)
INCLUDES="-I$V -I$D/shim_portable -I$D/shim_host -include $D/shim_host/orcdec_compat.h"
SRCS="$D/src/orcdec.cpp $D/src/orcdec_pocsag.cpp $D/src/orcdec_ft8.cpp $D/src/orcdec_js8.cpp $D/src/orcdec_lora.cpp $D/src/orcdec_sstv.cpp $D/src/orcdec_adsb.cpp $D/src/orcdec_acars.cpp $D/src/orcdec_p25.cpp $V/pocsag_decoder_core.cpp $V/adsb_decoder.cpp $V/acars_core.cpp $V/p25_decoder_core.cpp"
for f in ft8_native_backend ft8_demod ft8_pipeline ft8_spectral ft8_spectral_fft ft8_sync ft8_ldpc ft8_ldpc_decode ft8_message ft8_codec ft8_snr ft8_mode ft8_model; do
  SRCS="$SRCS $V/$f.cpp"
done
for f in lora_native_decoder lora_preamble_detector js8_codec js8_decoder js8_demod js8_fec js8_frame js8_frontend js8_message js8_mode js8_native_backend js8_osd js8_snr js8_spectral js8_sync sstv_receiver sstv_modes sstv_auto_detect; do
  SRCS="$SRCS $V/$f.cpp"
done
