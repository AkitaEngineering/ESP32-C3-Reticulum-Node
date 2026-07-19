# The root ESP-IDF environment is a minimal bare-metal sanity build.
# Source selection for ESP-IDF must happen here because PlatformIO ignores
# build_src_filter when framework = espidf.
idf_component_register(SRCS "APRS.cpp" "AX25.cpp" "LinkManager.cpp" "ReticulumPacket.cpp" "Link.cpp" "ReticulumNode.cpp" "InterfaceManager.cpp" "RoutingTable.cpp" "WebServer.cpp" "Config.cpp" "bare_blink.c")
