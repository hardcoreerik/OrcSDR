# Receiver audio demodulation (WFM, NFM, AM). Pure C++, host-tested; see tests/.
idf_component_register(
    SRCS "src/demod.cpp"
    INCLUDE_DIRS "include"
)
target_compile_features(${COMPONENT_LIB} PRIVATE cxx_std_17)
