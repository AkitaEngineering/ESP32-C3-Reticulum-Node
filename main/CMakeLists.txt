if(DEFINED ENV{PIOENV})
	# PlatformIO's esp-idf bare environment provides its own app_main source.
else()
	idf_component_register(SRCS "main.c" INCLUDE_DIRS ".")
endif()

