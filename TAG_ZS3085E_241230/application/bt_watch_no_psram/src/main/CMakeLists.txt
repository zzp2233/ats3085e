# SPDX-License-Identifier: Apache-2.0
# 本文件列出主应用编译进 app 的源文件，以及 P4 诊断构建额外使用的输入和宏。只有上层选择 main 时才会执行。

# 将本目录加入头文件搜索路径；实现写在头文件里时，归属包含它的 C 编译单元。
zephyr_include_directories(.)

# 主入口、启动器、输入和事件分派分别编译为目标文件。
target_sources(app PRIVATE system_app_main.c)
target_sources(app PRIVATE system_app_launcher.c)
target_sources(app PRIVATE system_app_input.c)
target_sources(app PRIVATE system_app_event.c)
# 音频策略受 CONFIG_AUDIO 控制，后面的提示音策略、事件映射及 FCC 入口随 main 一同编译。
target_sources_ifdef(CONFIG_AUDIO app PRIVATE system_audio_policy.c)
target_sources(app PRIVATE system_tts_policy.c)
target_sources(app PRIVATE system_event_map.c)
target_sources(app PRIVATE system_fcc_entry.c)
# Shell 命令表归入 system_app_shell.c.obj；其中的 CONFIG_SHELL、板级 CONFIG_SPI_2 和 CONFIG_FILE_SYSTEM 再决定是否登记 spi2ram。
target_sources(app PRIVATE system_app_shell.c)

# 显式指定 P4_BUNDLE_DIR 才启用本地验证，样本和密钥由仓库外的目录提供。
# 普通构建不嵌入这些录音、凭据或专用上传命令。
if(P4_BUNDLE_DIR)
  # 要求外部目录提供可编译的样本源文件；缺失时在 CMake 配置阶段立即报错。
  if(NOT EXISTS "${P4_BUNDLE_DIR}/p4_fixture.c")
    message(FATAL_ERROR "P4_BUNDLE_DIR does not contain p4_fixture.c")
  endif()
  # 启用内嵌验证分支，并让编译器为本目标输出各函数的栈使用统计。
  target_compile_definitions(app PRIVATE P4_EMBEDDED_VALIDATION=1)
  target_compile_options(app PRIVATE -fstack-usage)
  # 三组独立诊断控制观测、持续传输和额外状态采集；持续传输同时启用诊断宏。
  if(P4_SPI_WITNESS_TEST)
    target_compile_definitions(app PRIVATE P4_SPI_WITNESS_TEST=1)
  endif()
  if(P4_SPI_SOAK)
    target_compile_definitions(app PRIVATE P4_SPI_SOAK=1 P4_SPI_DIAG=1)
  endif()
  if(P4_SPI_DIAG)
    target_compile_definitions(app PRIVATE P4_SPI_DIAG=1)
  endif()
  # 启动同步与射频测试各自向应用编译单元传入相应的条件宏。
  if(P4_SPI_BOOT_SYNC)
    target_compile_definitions(app PRIVATE SPI_LINK_BOOT_SYNC=1)
  endif()
  if(P4_RADIO_TEST)
    target_compile_definitions(app PRIVATE P4_RADIO_TEST=1)
  endif()
  # 指定测试频率时仅接收下列两个十进制赫兹值，未经检查的值不会传给编译器。
  if(DEFINED P4_SPI_TEST_HZ)
    if(NOT "${P4_SPI_TEST_HZ}" STREQUAL "2300000" AND NOT "${P4_SPI_TEST_HZ}" STREQUAL "4000000")
      message(FATAL_ERROR "P4_SPI_TEST_HZ must be 2300000 or 4000000")
    endif()
    target_compile_definitions(app PRIVATE P4_SPI_TEST_HZ=${P4_SPI_TEST_HZ})
  endif()
  # 加入外部样本的头文件和源文件，并编译本项目的 JSON 处理及上传状态机。
  target_include_directories(app PRIVATE "${P4_BUNDLE_DIR}")
  target_sources(app PRIVATE p4_json.c p4_upload.c "${P4_BUNDLE_DIR}/p4_fixture.c")
endif()
# UI 的低电量、欢迎和关机界面在启用 UI 管理器时编译；随机输入测试有自己的开关。
target_sources_ifdef(CONFIG_UI_MANAGER app PRIVATE system_lpower_msgbox_view.c)
target_sources_ifdef(CONFIG_UI_MANAGER app PRIVATE system_welcome_view.c)
target_sources_ifdef(CONFIG_UI_MANAGER app PRIVATE system_goodbye_view.c)
target_sources_ifdef(CONFIG_MONKEY_TEST app PRIVATE system_monkey_test.c)

