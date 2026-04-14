# The root ESP-IDF environment is a minimal bare-metal sanity build.
# Source selection for ESP-IDF must happen here because PlatformIO ignores
# build_src_filter when framework = espidf.
idf_component_register(SRCS "bare_blink.c")
