# SPDX-License-Identifier: Apache-2.0

FILE(GLOB widget_sources *.c)

zephyr_include_directories(
	.
)

target_sources(app PRIVATE ${widget_sources})
