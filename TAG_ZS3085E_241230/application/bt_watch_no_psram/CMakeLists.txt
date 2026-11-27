# SPDX-License-Identifier: Apache-2.0
# 本文件是 bt_watch_no_psram 应用的 CMake 入口。它接入 Zephyr、链接板上使用的测试库，再进入 src 选择源码。

#list(APPEND ZEPHYR_EXTRA_MODULES ${CMAKE_CURRENT_SOURCE_DIR}/../../modules/framework)


cmake_minimum_required(VERSION 3.13.1)

# 沿 ZEPHYR_BASE 查找当前 SDK 的构建规则，Zephyr 在配置阶段提供 app 目标。
find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(bt_watch_no_psram)

# 下列头文件目录对 app 自身有效，供预编译蓝牙生产测试库的调用端编译。
target_include_directories(app PRIVATE
	./prebuild/bt_production_test
)

# 把生产测试库列入 app 的链接接口，让最终固件链接时可以解析其中的符号。
target_link_libraries(app INTERFACE ${CMAKE_CURRENT_LIST_DIR}/prebuild/bt_production_test/libbt_test.a)

# 下一层根据 Kconfig 值选择应用子目录，主应用代码位于 src/main。
add_subdirectory(src)

# 启用栈保护时，通过当前 Zephyr 库的编译选项定义 APP_BT_WATCH 并要求 GCC 检查所有函数栈。
if(CONFIG_STACK_CANARIES)
	zephyr_library_compile_options(-DAPP_BT_WATCH -fstack-protector-all)
endif()
