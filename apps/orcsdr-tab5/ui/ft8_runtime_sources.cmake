# Shared native FT8/FT4/JS8 runtime and decoder sources.
# Keep UI, storage, hunter, and dashboard glue in their owning application.
set(_ORCSDR_FT8_RUNTIME_SOURCE_NAMES
    ft8_runtime.cpp
    ft8_audio_tap.cpp
    ft8_native_backend.cpp
    ft8_model.cpp
    ft8_conditions.cpp
    ft8_mode.cpp
    ft8_sync.cpp
    ft8_demod.cpp
    ft8_spectral_fft.cpp
    ft8_pipeline.cpp
    ft8_snr.cpp
    ft8_message.cpp
    ft8_codec.cpp
    ft8_ldpc.cpp
    ft8_ldpc_decode.cpp
    ft8_clock_estimator.cpp
    js8_mode.cpp
    js8_frame.cpp
    js8_sync.cpp
    js8_spectral.cpp
    js8_demod.cpp
    js8_frontend.cpp
    js8_fec.cpp
    js8_codec.cpp
    js8_message.cpp
    js8_osd.cpp
    js8_decoder.cpp
    js8_snr.cpp
    js8_native_backend.cpp
    wwv_tick.cpp
)

set(ORCSDR_FT8_RUNTIME_SOURCES)
foreach(_source_name IN LISTS _ORCSDR_FT8_RUNTIME_SOURCE_NAMES)
    get_filename_component(_source_path "${CMAKE_CURRENT_LIST_DIR}/${_source_name}" ABSOLUTE)
    list(APPEND ORCSDR_FT8_RUNTIME_SOURCES "${_source_path}")
endforeach()
unset(_source_name)
unset(_source_path)
unset(_ORCSDR_FT8_RUNTIME_SOURCE_NAMES)
