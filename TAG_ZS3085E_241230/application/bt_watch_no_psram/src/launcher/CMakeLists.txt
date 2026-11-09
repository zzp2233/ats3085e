# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(
	.
)

target_sources(app PRIVATE launcher_app.c)

if (CONFIG_UI_MANAGER)

FILE(GLOB common_src common/*.c)
FILE(GLOB applist_src applist/*.c)
FILE(GLOB clocksel_src clock_selector/*.c)
FILE(GLOB desktop_src desktop/*.c)
FILE(GLOB health_src health/*.c)
FILE(GLOB longlist_src longlist/*.c)
FILE(GLOB power_src power/*.c)
FILE(GLOB stopwatch_src stopwatch/*.c)

target_sources(app PRIVATE
	alarm_set_ui.c
	power_ui.c
	stopwatch_ui.c
	setting_ui.c
	${common_src}
	${applist_src}
	${clocksel_src}
	${desktop_src}
	${health_src}
	${longlist_src}
	${power_src}
	${stopwatch_src}
)

target_include_directories(app PRIVATE .)

add_subdirectory(alipay)
add_subdirectory(gps)
add_subdirectory(vibration)

endif()
