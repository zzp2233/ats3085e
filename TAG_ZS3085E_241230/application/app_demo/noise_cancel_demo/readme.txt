本示例功能：
演示本地录音降噪功能，可通过串口指令输入输出文件路径名字。默认录制编码为opus格式。

例如：
输出文件路径名字：/NOR:/record.opus
串口指令：
开始处理：app nc start /NOR:/record.opus
停止处理：app nc stop

备注：上电后会自动进入U盘模式，按on/off键退出U盘模式后才能使用指令录音