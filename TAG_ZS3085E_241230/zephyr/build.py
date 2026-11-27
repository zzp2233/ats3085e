#!/usr/bin/env python
#
# Copyright (c) 2020 Actions Semiconductor Co., Ltd
#
# SPDX-License-Identifier: Apache-2.0
#
# 本文件是 ATS SDK 的选板、编译与固件打包入口。从当前 zephyr 目录运行 python build.py -s，脚本读取 .build_config，默认用 west 编译，再用板型和应用资源打包。

import os
import sys
import glob
import time
import argparse
import platform
import struct
import shutil
import subprocess
import re
import xml.etree.ElementTree as ET

# 下列目录以启动脚本时的工作目录为根。应用与 zephyr 同级，板级定义和厂商打包工具位于 zephyr 内。
sdk_arch = "arm"
sdk_root = os.getcwd()
sdk_root_parent = os.path.dirname(sdk_root)
#sdk_mcuboot_dir=os.path.join(sdk_root_parent, "bootloader", "mcuboot")
#sdk_mcuboot_imgtool = os.path.join(sdk_mcuboot_dir, "scripts", "imgtool.py")
#sdk_mcuboot_key= os.path.join(sdk_mcuboot_dir, "root-rsa-2048.pem")

#sdk_mcuboot_app_path=os.path.join(sdk_mcuboot_dir, "boot", "zephyr")
sdk_boards_dir = os.path.join(sdk_root, "boards", sdk_arch)
#sdk_application_dir = os.path.join(sdk_root_parent, "application")
sdk_application_dir=""
sdk_app_dir = os.path.join(sdk_root_parent, "application")
sdk_samples_dir = os.path.join(sdk_root, "samples")
sdk_tests_dir = os.path.join(sdk_root, "tests")
sdk_script_dir = os.path.join(sdk_root, "tools")
sdk_script_prebuilt_dir = os.path.join(sdk_script_dir, "prebuilt")
build_config_file = os.path.join(sdk_root, ".build_config")

# 记录可选的应用、子应用、板型和配置名；实际选项在 main 中读入或交互确定。
application = ""
sub_app = ""
board_conf = ""
app_conf = ""

# 判断运行环境是否走 Windows 工具链分支，包括 MSYS、MinGW 和 Cygwin 的系统名。
def is_windows():
    sysstr = platform.system()
    if (sysstr.startswith('Windows') or \
       sysstr.startswith('MSYS') or     \
       sysstr.startswith('MINGW') or    \
       sysstr.startswith('CYGWIN')):
        return True
    else:
        return False

# 将菜单输入优先按整数解释，失败时再尝试浮点数并向零取整；两次转换都失败则返回 False。
def to_int(str):
    try:
        int(str)
        return int(str)
    except ValueError:
        try:
            float(str)
            return int(float(str))
        except ValueError:
            return False

# 显示 path 下第一层目录并返回选中目录的名字；调用方须提供至少一个可选目录。
def read_choice(  tip, path):
    print("")
    print(" %s" %(tip))
    print("")
    dirs = next(os.walk(path))[1]
    i = 0

    # 打印从 1 起算的编号；显示顺序沿用 os.walk 返回的目录顺序。
    for file in dirs:
        file_path = os.path.join(path, file)
        if os.path.isdir(file_path):
            i += 1
            print("    %d. %s" %(i, file))

    print("")
    input_prompt = "Which would you like? [" + dirs[0] + "] "
    str = ""
    try:
        str = input(input_prompt)
    except Exception as ex:
        print("")
    # 无输入、无法转换或转换结果为 0 时回到第一项；超过最大编号时取末项。
    if to_int(str) != False:
        j = to_int(str)
    else:
        j = 1

    if j > i:
        j = i
    j -= 1
    return dirs[j]


# 执行命令参数列表并等待退出，返回合并后的原始输出字节和进程退出码。调用方据此决定是否报告错误。
def run_cmd(cmd):
    """Echo and run the given command.

    Args:
    cmd: the command represented as a list of strings.
    Returns:
    A tuple of the output and the exit code.
    """
#    print("Running: ", " ".join(cmd))
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    output, _ = p.communicate()
    #print("%s" % (output.rstrip()))
    return (output, p.returncode)

# 用板级和应用 nvram.prop 生成 out_dir/nvram.bin。应用配置目录优先于应用根目录；任一必需输入缺失则跳过本镜像。
def build_nvram_bin(out_dir, board_dir, app_dir, app_cfg_dir):
    nvram_path = os.path.join(out_dir, "nvram.bin")
    app_cfg_path =  os.path.join(app_cfg_dir, "nvram.prop")
    if os.path.exists(app_cfg_path) == False:
        app_cfg_path =  os.path.join(app_dir, "nvram.prop")
    board_cnf_path =  os.path.join(board_dir, "nvram.prop")
    print("\n build_nvram \n")
    if not os.path.isfile(board_cnf_path):
        print("\n  board nvram %s not exists\n" %(board_cnf_path))
        return
    if not os.path.isfile(app_cfg_path):
        print("\n  app nvram %s not exists\n" %(app_cfg_path))
        return
    script_nvram_path = os.path.join(sdk_script_dir, 'build_nvram_bin.py')
    # 参数顺序由下游工具解释；外部进程失败时显示其输出并终止打包。
    cmd = ['python', '-B', script_nvram_path,  '-o', nvram_path, app_cfg_path, board_cnf_path]
    (outmsg, exit_code) = run_cmd(cmd)
    if exit_code !=0:
        print('make build_nvram_bin error')
        print(outmsg.decode('utf-8'))
        sys.exit(1)
    print("\n build_nvram  finshed\n\n")

# 将 in_dir 的文件复制到 out_dir。目标已存在时按文件名平铺覆盖；首次创建目标则用 copytree 保留目录树。
def dir_cp(out_dir, in_dir):
    print(" cp dir %s  to dir %s"  %(in_dir, out_dir))
    if os.path.exists(in_dir) == True:
        if os.path.exists(out_dir) == True:
            for parent, dirnames, filenames in os.walk(in_dir):
                for filename in filenames:
                    pathfile = os.path.join(parent, filename)
                    shutil.copyfile(pathfile, os.path.join(out_dir, filename))
        else:
            shutil.copytree(in_dir, out_dir)

# 按输入的相对目录结构复制文件。合并已有输出时按需创建子目录，同名文件覆盖。
def dir_tree_cp(out_dir, in_dir):
    # 保留相对目录层级，让不同子目录下的同名文件仍可区分。
    print(" cp dir %s  to dir %s"  %(in_dir, out_dir))
    if os.path.exists(in_dir):
        if os.path.exists(out_dir):
            for parent, dirnames, filenames in os.walk(in_dir):
                for filename in filenames:
                    copy_to_dir = os.path.join(out_dir, os.path.relpath(parent, in_dir))
                    if not os.path.isdir(copy_to_dir):
                        os.mkdir(copy_to_dir)
                    pathfile = os.path.join(parent, filename)
                    shutil.copyfile(pathfile, os.path.join(copy_to_dir, filename))
        else:
            shutil.copytree(in_dir, out_dir)

# 在 in_dir 查找 name；普通文件复制到 out_dir/name，目录递归合并到 out_dir，源不存在则跳过。
def check_cp(out_dir, in_dir, name):
    in_cp = os.path.join(in_dir, name)
    out_cp = os.path.join(out_dir, name)
    if os.path.exists(in_cp):
        if os.path.isfile(in_cp) == True:
            print(" cp file %s  to dir %s" %(in_cp,out_cp))
            shutil.copyfile(in_cp, out_cp)
        else :
            dir_tree_cp(out_dir, in_cp)

# 汇集一个具名资源卷，按板级、应用、应用配置顺序复制内容，再调用 build_sdfs.py 生成同名 .bin。
def build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir, sdfs_name):
    board_sdfs_dir = os.path.join(board_dir, sdfs_name)
    app_sdfs_dir = os.path.join(app_dir, sdfs_name)
    app_cfg_sdfs_dir = os.path.join(app_cfg_dir, sdfs_name)
    #print("\n build_sdfs : sdfs_dir %s, board dfs %s\n\n" %(sdfs_dir, board_sdfs_dir))
    # 三处均无该卷目录时直接跳过，避免为未使用的卷生成镜像。
    if (os.path.exists(board_sdfs_dir) == False) \
        and (os.path.exists(app_sdfs_dir) == False) \
        and (os.path.exists(app_cfg_sdfs_dir) == False):
        print("\n build_sdfs :  %s not exists\n\n" %(sdfs_name))
        return

    sdfs_dir = os.path.join(out_dir, sdfs_name)
    # 删除这一卷上次的暂存目录后重新收集输入，避免混入已移除的资源文件。
    if os.path.exists(sdfs_dir) == True:
        shutil.rmtree(sdfs_dir)
        time.sleep(0.1)

    if os.path.exists(board_sdfs_dir) ==  True:
        dir_cp(sdfs_dir, board_sdfs_dir)

    if os.path.exists(app_sdfs_dir) == True:
        dir_cp(sdfs_dir, app_sdfs_dir)

    if os.path.exists(app_cfg_sdfs_dir) == True:
        dir_cp(sdfs_dir, app_cfg_sdfs_dir)

    print("\n build_sdfs : sdfs_dir %s, sdfs_name %s\n\n" %(sdfs_dir, sdfs_name))
    # 字体和图形资源卷移除 txt 子目录及卷根的 .txt 文件，这些文本不进入输出镜像。
    if (sdfs_name == 'fonts') or (sdfs_name == 'res') or (sdfs_name == 'res_b'):
        txtdir = os.path.join(sdfs_dir, "txt")
        if os.path.exists(txtdir) == True:
            shutil.rmtree(txtdir)
        for infile in glob.glob(os.path.join(sdfs_dir, '*.txt')):
            print(" remove file :  %s\n" %(infile))
            os.remove(infile)

    # 为 ksdfs 或 sdfs 打包时，如存在待打包的内核符号表，将它移入当前卷目录。
    if (sdfs_name == 'ksdfs') or (sdfs_name == 'sdfs'):
        ksym_bin = os.path.join(out_dir, "ksym.bin")
        if os.path.exists(ksym_bin):
            shutil.move(ksym_bin, os.path.join(sdfs_dir, "ksym.bin"))

    script_sdfs_path = os.path.join(sdk_script_dir, 'build_sdfs.py')
    sdfs_bin_path = os.path.join(out_dir, sdfs_name+".bin")
    cmd = ['python', '-B', script_sdfs_path,  '-o', sdfs_bin_path, '-d', sdfs_dir]
    (outmsg, exit_code) = run_cmd(cmd)
    if exit_code !=0:
        print('make build_sdfs_bin error')
        print(outmsg.decode('utf-8'))
        sys.exit(1)
    print("\n build_sdfs %s finshed\n\n" %(sdfs_name))

# 从板级和应用的 fs_sdfs 目录枚举额外资源卷；同名子目录合并为一个待打包名称。
def build_sdfs_bin_bydir(out_dir, board_dir, app_dir, app_cfg_dir):
    bpath = os.path.join(board_dir, "fs_sdfs");
    apath = os.path.join(app_cfg_dir, "fs_sdfs");
    # 应用配置目录没有 fs_sdfs 时，回退到应用根目录继续查找。
    if os.path.exists(apath) == False:
        apath = os.path.join(app_dir, "fs_sdfs");
    if (os.path.exists(bpath) == False) and (os.path.exists(apath) == False):
        print('not fs_sdfs')
        return
    all_dirs = {}
    if os.path.exists(bpath) == True:
        for file in os.listdir(bpath):
            print("board list %s ---\n" %(file))
            if os.path.isdir(os.path.join(bpath,file)):
                all_dirs[file] = file
    if os.path.exists(apath) == True:
        for file in os.listdir(apath):
            print("app list %s ---\n" %(file))
            if os.path.isdir(os.path.join(apath,file)):
                all_dirs[file] = file
    # 逐一处理两个目录的并集，同一卷只发起一次打包。
    for dir in all_dirs.keys():
        print("--build_sdfs %s ---\n" %(dir))
        build_sdfs_bin_name(out_dir, bpath, apath, app_cfg_dir, dir)


# 先尝试内置的六类卷，再处理 fs_sdfs 中额外定义的目录。每个卷按实际目录是否存在决定生成与否。
def build_sdfs_bin(out_dir, board_dir, app_dir, app_cfg_dir):
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"ksdfs")
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"sdfs")
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"fatfs")
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"res")
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"fonts")
    build_sdfs_bin_name(out_dir, board_dir, app_dir, app_cfg_dir,"res_b")
    build_sdfs_bin_bydir(out_dir, board_dir, app_dir, app_cfg_dir)

# 将 file_add 的全部字节追加到 filename 末尾。任一文件不存在时直接返回。
def file_link(filename, file_add):
    if not os.path.exists(filename):
        return
    if not os.path.exists(file_add):
        return
    print("file %s, link %s \n" %(filename, file_add))
    with open(file_add, 'rb') as fa:
        file_data = fa.read()
        fa.close()
    fsize = os.path.getsize(filename)
    with open(filename, 'rb+') as f:
        f.seek(fsize, 0)
        f.write(file_data)
        f.close()

# 扫描 cfg_path 中未注释的 CONFIG_KALLSYMS 赋值。判断依据是键名存在，代码没有进一步比较等号右侧的值。
def is_config_KALLSYMS(cfg_path):
    bret = False
    print("\n cfg_path =%s\n" %cfg_path)
    fp = open(cfg_path,"r")
    lines = fp.readlines()
    for elem in lines:
        if elem[0] != '#':
            _c = elem.strip().split("=")
            if _c[0] == "CONFIG_KALLSYMS":
                print("CONFIG_KALLSYMS");
                bret = True
                break;
    fp.close
    print("\nCONFIG_KALLSYMS=%d\n" %bret )
    return bret

# 按小端格式写 512 字节 MBR。udisk_offset 与 udisk_size 以字节传入，第一分区的起点和长度换为 512 字节扇区数。
def build_mbr_bin(mbr_file, udisk_offset, udisk_size):
    bootcode = b'\x00' * 446
    part1 = struct.pack("<BBBBBBBBII", 0x0, 0x01, 0x01, 0x00, \
                0xb, 0xfe, 0xff, 0xff, udisk_offset//512, udisk_size//512)
    part2 = struct.pack("<BBBBBBBBII", 0, 0, 0, 0, \
                0, 0, 0, 0, 0, 0)
    part3 = struct.pack("<BBBBBBBBII", 0, 0, 0, 0, \
                0, 0, 0, 0, 0, 0)
    part4 = struct.pack("<BBBBBBBBII", 0, 0, 0, 0, \
                0, 0, 0, 0, 0, 0)
    id = struct.pack("<BB", 0x55, 0xaa)
    # 依次写入 446 字节引导区、四条 16 字节分区记录以及 55 AA 签名，后三条分区记录清零。
    with open(mbr_file, 'wb') as f:
        f.write(bootcode)
        f.write(part1)
        f.write(part2)
        f.write(part3)
        f.write(part4)
        f.write(id)

# 根据 firmware.xml 的 udisk 分区描述生成 MBR，并从应用资源目录制作指定容量的数据盘镜像。
def build_datafs_bin(out_dir, app_dir, app_cfg_dir, fw_cfgfile):
    script_datafs_path = os.path.join(sdk_script_dir, 'build_datafs.py')

    tree = ET.ElementTree(file=fw_cfgfile)
    root = tree.getroot()
    if (root.tag != 'firmware'):
        sys.stderr.write('error: invalid firmware config file')
        sys.exit(1)

    part_list = root.find('partitions').findall('partition')
    # 将每个 partition 的子元素整理成名称到文本的映射，供地址、容量和输出文件名查找。
    for part in part_list:
        part_prop = {}
        for prop in part:
            part_prop[prop.tag] = prop.text.strip()
        # 仅处理具备 storage_id 的 udisk 分区；先按 XML 生成分区表，再查找该卷的数据目录。
        if ('storage_id' in part_prop.keys()) and (part_prop['name'] == 'udisk'):
            mbr_bin_path = os.path.join(out_dir, 'u_mbr.bin')
            build_mbr_bin(mbr_bin_path, int(part_prop['address'], 0), int(part_prop['size'], 0))
            app_datafs_dir = os.path.join(app_cfg_dir, part_prop['name'])
            if os.path.exists(app_datafs_dir) == False:
                app_datafs_dir = os.path.join(app_dir, part_prop['name'])
                if os.path.exists(app_datafs_dir) == False:
                    print("\n build_datafs : app config datafs %s not exists\n\n" %(app_datafs_dir))
                    continue
            datafs_cap = int(part_prop['size'], 0)
            datafs_bin_path = os.path.join(out_dir, part_prop['file_name'])
            print('DATAFS (%s) capacity => 0x%x' %(datafs_bin_path, datafs_cap))
            # 通过命令参数把目标容量、文件名和素材目录传入文件系统工具。
            cmd = ['python', '-B', script_datafs_path,  '-o', datafs_bin_path, '-s', str(datafs_cap), '-d', app_datafs_dir]
            (outmsg, exit_code) = run_cmd(cmd)
            if exit_code !=0:
                print('make build_datafs_bin %s error' %(datafs_bin_path))
                print(outmsg.decode('utf-8'))
                sys.exit(1)

    print("\n build_datafs finshed\n\n")

# 从最终 .config 读取 LittleFS 块大小（字节），未找到显式定义时返回 4096。
def read_littlefs_block_size_config(cfg_path):
    block_size = 4096
    fp = open(cfg_path,"r")
    lines = fp.readlines()
    for elem in lines:
        if elem[0] != '#':
            _c = elem.strip().split("=")
            if _c[0] == "CONFIG_FS_LITTLEFS_BLOCK_SIZE":
                block_size = int(_c[1].strip('"'), 0)
                break;
    fp.close
    return block_size

# 按有 storage_id 的 littlefs 分区制作镜像。卷容量、文件名取自 XML，块大小取自这一板型的 zephyr/.config。
def build_littlefs_bin(out_dir, app_dir, app_cfg_dir, fw_cfgfile):
    script_littlefs_path = os.path.join(sdk_script_dir, 'build_littlefs.py')
    OS_OUTDIR = os.path.join(out_dir, "../../zephyr")

    tree = ET.ElementTree(file=fw_cfgfile)
    root = tree.getroot()
    if (root.tag != 'firmware'):
        sys.stderr.write('error: invalid firmware config file')
        sys.exit(1)

    part_list = root.find('partitions').findall('partition')
    for part in part_list:
        part_prop = {}
        for prop in part:
            part_prop[prop.tag] = prop.text.strip()
        # 优先读取应用配置子目录中的 littlefs，缺失时回退到应用根目录；两处均无素材则跳过该分区。
        if ('storage_id' in part_prop.keys()) and (part_prop['name'] == 'littlefs'):
            app_littlefs_dir = os.path.join(app_cfg_dir, part_prop['name'])
            if os.path.exists(app_littlefs_dir) == False:
                app_littlefs_dir = os.path.join(app_dir, part_prop['name'])
                if os.path.exists(app_littlefs_dir) == False:
                    print("\n build_datafs : app config littlefs %s not exists\n\n" %(app_littlefs_dir))
                    continue
            littlefs_cap = int(part_prop['size'], 0)
            littlefs_bin_path = os.path.join(out_dir, part_prop['file_name'])
            blocksize = read_littlefs_block_size_config(os.path.join(OS_OUTDIR, ".config"))
            print('LITTLEFS (%s) capacity => 0x%x' %(littlefs_bin_path, littlefs_cap))
            # 将卷容量和块大小作为独立参数交给 LittleFS 制作工具。
            cmd = ['python', '-B', script_littlefs_path, '-o', littlefs_bin_path, '-s', str(littlefs_cap), '-d', app_littlefs_dir, '-b', str(blocksize)]
            (outmsg, exit_code) = run_cmd(cmd)
            if exit_code !=0:
                print('make build_littlefs_bin %s error' %(littlefs_bin_path))
                print(outmsg.decode('utf-8'))
                sys.exit(1)

    print('\n build_littlefs finshed \n\n')

# 读出 cfg_path 中首个与 cfg_name 匹配的未注释配置值，未找到时返回字符串 0。
def read_val_by_config(cfg_path, cfg_name):
    val = "0"
    fp = open(cfg_path,"r")
    lines = fp.readlines()
    for elem in lines:
        if elem[0] != '#':
            _c = elem.strip().split("=")
            if _c[0] == cfg_name:
                val = _c[1].strip('"')
                break;
    fp.close
    return val

# 读取 CONFIG_SOC_SERIES 以选择 SoC 预置资源目录，没有找到时使用 lark。
def read_soc_by_config(cfg_path):
    soc = "lark"
    fp = open(cfg_path,"r")
    lines = fp.readlines()
    for elem in lines:
        if elem[0] != '#':
            _c = elem.strip().split("=")
            if _c[0] == "CONFIG_SOC_SERIES":
                soc = _c[1].strip('"')
                break;
    fp.close
    return soc

# 若指定镜像存在，交由 pack_app_image.py 就地处理镜像头与校验；不存在时跳过。
def image_add_checksum(fw_dir_bin, img_bin):
    img_bin_file = os.path.join(fw_dir_bin, img_bin)
    if os.path.exists(img_bin_file) == False:
        return
    print("\nFW: Post build %s\n"%img_bin)
    script_firmware_path = os.path.join(sdk_script_dir, 'pack_app_image.py')
    cmd = ['python', '-B', script_firmware_path, img_bin_file]
    print("\n build cmd : %s\n\n" %(cmd))
    (outmsg, exit_code) = run_cmd(cmd)
    if exit_code !=0:
        print("pack %s error\n"%img_bin)
        print(outmsg)
        sys.exit(1)


# 以 board 和 out_dir 确定一套烧录包，将本次编译的 zephyr.bin、板级预置镜像、分区 XML 及应用资源汇总到 _firmware。
def build_firmware(board, out_dir, board_dir, app_dir, app_cfg_dir):
    print("\n build_firmware : out %s, board %s, app_dir %s cfg_dir %s\n\n" %(out_dir, board_dir, app_dir, app_cfg_dir))
    # res_files 指资源卷，full_files 指整包 OTA 描述；zephyr 保存链接产物，_firmware/bin 是打包暂存目录。
    res_files = ["fonts", "res", "res_b"]
    full_files = ["full","firmware"]
    FW_DIR = os.path.join(out_dir, "_firmware")
    OS_OUTDIR = os.path.join(out_dir, "zephyr")
    if os.path.exists(FW_DIR) == False:
        os.mkdir(FW_DIR)
    FW_DIR_BIN = os.path.join(FW_DIR, "bin")
    if os.path.exists(FW_DIR_BIN) == False:
        os.mkdir(FW_DIR_BIN)

    # 从本次构建的 .config 决定是否把内核符号表送入资源卷。
    if is_config_KALLSYMS(os.path.join(OS_OUTDIR, ".config")):
        print("cpy ksym.bin- to fw dir-");
        check_cp(FW_DIR_BIN , OS_OUTDIR, "ksym.bin")


    # 生成板级配置和资源卷，然后按 SoC 系列合并厂商通用、系列及预置资源。
    build_nvram_bin(FW_DIR_BIN, board_dir, app_dir, app_cfg_dir)
    build_sdfs_bin(FW_DIR_BIN, board_dir, app_dir, app_cfg_dir)
    soc = read_soc_by_config(os.path.join(OS_OUTDIR, ".config"))
    print("\n build_firmware : soc name= %s\n" %(soc))
    dir_cp(FW_DIR_BIN, os.path.join(sdk_script_prebuilt_dir, soc, "common", "bin"))
    dir_cp(FW_DIR_BIN, os.path.join(sdk_script_prebuilt_dir, soc, "bin"))
    dir_tree_cp(FW_DIR_BIN, os.path.join(sdk_script_prebuilt_dir, soc, "prebuild"))

    fw_cfgfile = os.path.join(FW_DIR, "firmware.xml")

    # 依次尝试板级、应用和配置子目录的布局，后复制的同名文件生效。
    check_cp(FW_DIR, board_dir, "firmware.xml")
    check_cp(FW_DIR, app_dir, "firmware.xml")
    check_cp(FW_DIR, app_cfg_dir, "firmware.xml")
    # 调试包布局可由当前应用配置单独提供。
    check_cp(FW_DIR, app_cfg_dir, "firmware_debug.xml")
    # 主分区布局 firmware.xml 是必需输入，即使存在可选调试配置也不能省略。
    if not os.path.exists(fw_cfgfile):
        print("failed to find out firmware.xml")
        sys.exit(1)

    # 以下旧的单一资源布局复制路径已停用。
    #res_cfgfile = os.path.join(FW_DIR, "firmware_res.xml")
    #check_cp(FW_DIR, app_dir, "firmware_res.xml")
    #check_cp(FW_DIR, app_cfg_dir, "firmware_res.xml")
    
    # 收集各资源卷的 OTA 配置；应用配置目录的同名文件可覆盖应用根目录的版本。
    for res_file in res_files:
        res_cfgfile = os.path.join(FW_DIR, "ota_"+ res_file +".xml")
        check_cp(FW_DIR, app_dir, "ota_"+ res_file +".xml")
        check_cp(FW_DIR, app_cfg_dir, "ota_"+ res_file +".xml")

    # 整包和固件 OTA 各有一份可选的 XML。
    for full_file in full_files:
        res_cfgfile = os.path.join(FW_DIR, "ota_"+ full_file +".xml")
        check_cp(FW_DIR, app_dir, "ota_"+ full_file +".xml")
        check_cp(FW_DIR, app_cfg_dir, "ota_"+ full_file +".xml")

    # 先复制板级引导、恢复和产品输入，再以应用及其专用配置覆盖暂存目录。
    check_cp(FW_DIR_BIN, board_dir, "adfus_u.bin")
    check_cp(FW_DIR_BIN, board_dir, "adfus.bin")
    check_cp(FW_DIR_BIN, board_dir, "mbrec.bin")
    check_cp(FW_DIR_BIN, board_dir, "afi.bin")
    check_cp(FW_DIR_BIN, board_dir, "bootloader.ini")
    check_cp(FW_DIR_BIN, board_dir, "nand_id.bin")
    check_cp(FW_DIR_BIN, board_dir, "recovery.bin")
    check_cp(FW_DIR_BIN, board_dir, "fw_product.cfg")
    check_cp(FW_DIR_BIN, app_dir, "prebuild")
    check_cp(FW_DIR_BIN, app_cfg_dir, "prebuild")
    check_cp(FW_DIR_BIN, app_cfg_dir, "E_CHECK.FW")
    check_cp(FW_DIR_BIN, app_cfg_dir, "fwimage.cfg")

    ota_app_path = os.path.join(FW_DIR_BIN, "ota_app.bin")
    # 先清除暂存目录内的旧 ota_app.bin，再尝试引入板级版本，避免复用过期镜像。
    if os.path.exists(ota_app_path) == True:
         os.remove(ota_app_path)
    check_cp(FW_DIR_BIN, board_dir, "ota_app.bin")

    # 如果板级提供预签名 mbrec.bin，将其作为引导镜像加工的输入。
    sign_dir =os.path.join(board_dir, "sign-img")
    if os.path.exists(sign_dir) == True:
        check_cp(FW_DIR_BIN, sign_dir, "mbrec.bin")


    print("\nFW: Post build mbrec.bin\n")
    # 用板级 bootloader.ini 和 NAND 标识对引导镜像进行打包处理。
    script_firmware_path = os.path.join(sdk_script_dir, 'build_boot_image.py')
    cmd = ['python', '-B', script_firmware_path, os.path.join(FW_DIR_BIN, "mbrec.bin"), \
            os.path.join(FW_DIR_BIN, "bootloader.ini"), \
            os.path.join(FW_DIR_BIN, "nand_id.bin")]
    print("\n build cmd : %s\n\n" %(cmd))
    (outmsg, exit_code) = run_cmd(cmd)
    if exit_code !=0:
        print('make build_boot_image error')
        print(outmsg.decode('utf-8'))
        sys.exit(1)


    # 按最终分区配置制作可选的 udisk 与 LittleFS 数据镜像。
    build_datafs_bin(FW_DIR_BIN, app_dir, app_cfg_dir, fw_cfgfile)
    build_littlefs_bin(FW_DIR_BIN, app_dir, app_cfg_dir, fw_cfgfile)
    
    # 这里把本次链接导出的 zephyr.bin 复制为打包器识别的 bin/app.bin。
    app_bin_src_path = os.path.join(OS_OUTDIR, "zephyr.bin")
    # 未生成 zephyr.bin 时，这一函数会在此返回，不能用已有 .fw 的存在推断本次打包成功。
    if os.path.exists(app_bin_src_path) == False:
        print("\n app.bin not eixt=%s\n\n"  %(app_bin_src_path))
        return
    #app_bin_dst_path=os.path.join(FW_DIR_BIN, "zephyr.bin")
    app_bin_dst_path=os.path.join(FW_DIR_BIN, "app.bin")
    shutil.copyfile(app_bin_src_path, app_bin_dst_path)

    # 逐个处理资源校验信息，再为实际存在的应用、恢复和 OTA 镜像处理镜像头。

    for res_file in res_files:
        script_firmware_path = os.path.join(sdk_script_dir, 'pack_res_chksum.py')
        cmd = ['python', '-B', script_firmware_path, '-b', app_bin_dst_path, '-r', res_file + ".bin"]
        print("\n build cmd : %s\n\n" %(cmd))
        (outmsg, exit_code) = run_cmd(cmd)
        if exit_code !=0:
            print('pack app error')
            print(outmsg.decode('utf-8'))
            sys.exit(1)

    image_add_checksum(FW_DIR_BIN, "recovery.bin");
    image_add_checksum(FW_DIR_BIN, "app.bin");
    image_add_checksum(FW_DIR_BIN, "ota_app.bin");

    # 正式打包前，板级签名目录中的同名镜像可覆盖暂存版本。
    if os.path.exists(sign_dir) == True:
        check_cp(FW_DIR_BIN, sign_dir, "recovery.bin")
        check_cp(FW_DIR_BIN, sign_dir, "app.bin")
        check_cp(FW_DIR_BIN, sign_dir, "ota_app.bin")

    # 调用 build_firmware.py 生成普通包；板名来自选板结果，XML 来自上一步整理的布局文件。
    print("\n--board %s ==--\n\n"  %( board))
    script_firmware_path = os.path.join(sdk_script_dir, 'build_firmware.py')
    cmd = ['python', '-B', script_firmware_path,  '-c', fw_cfgfile, \
            '-e', os.path.join(FW_DIR_BIN, "encrypt.bin"),\
            '-ef', os.path.join(FW_DIR_BIN, "E_CHECK.FW"),\
            '-s', "lark",\
            '-b', board]
    print("\n build cmd : %s\n\n" %(cmd))
    (outmsg, exit_code) = run_cmd(cmd)
    if exit_code !=0:
        print('make build_firmware error\n')
        print(outmsg.decode('utf-8'))
        sys.exit(1)

    # 普通包生成后，重新以 zephyr.bin 准备 app.bin 并处理镜像头，供后续可选的调试包使用。
    app_bin_dst_path=os.path.join(FW_DIR_BIN, "app.bin")
    shutil.copyfile(app_bin_src_path, app_bin_dst_path)
    image_add_checksum(FW_DIR_BIN, "app.bin");

    # 调试打包准备阶段也优先使用板级提供的签名 app.bin。
    if os.path.exists(sign_dir) == True:
        check_cp(FW_DIR_BIN, sign_dir, "app.bin")

    # 若目录中存在调试布局，再调用同一打包器并传入 debug 后缀。
    fw_cfgfile = os.path.join(FW_DIR, "firmware_debug.xml")
    if os.path.exists(fw_cfgfile):
        cmd = ['python', '-B', script_firmware_path,  '-c', fw_cfgfile, \
                '-e', os.path.join(FW_DIR_BIN, "encrypt.bin"),\
                '-ef', os.path.join(FW_DIR_BIN, "E_CHECK.FW"),\
                '-s', "lark",\
                '-b', board,\
			    '-x', "debug"]
        print("\n build cmd : %s\n\n" %(cmd))
        (outmsg, exit_code) = run_cmd(cmd)
        if exit_code !=0:
            print('make build_firmware_debug error\n')
            print(outmsg.decode('utf-8'))
            sys.exit(1)

    # 清理暂存目录里为本次打包复制的 OTA XML，保留生成的镜像和烧录包。
    for res_file in res_files:
        ota_xml_path = os.path.join(FW_DIR,"ota_"+ res_file +".xml")
        if os.path.exists(ota_xml_path) == True:
             os.remove(ota_xml_path)

    for full_file in full_files:
        ota_xml_path = os.path.join(FW_DIR,"ota_"+ full_file +".xml")
        if os.path.exists(ota_xml_path) == True:
             os.remove(ota_xml_path)


# Windows 下调用 ninja2mdk.cmd 生成 Keil 工程；可附带库名，非静默模式会打开工程，随后退出脚本。
def build_zephyr_app_by_keil(board, out_dir, app_dir, app_cfg_p, libname, silent):
    if (is_windows()):
        os.chdir(sdk_root)
        build_args = "ninja2mdk.cmd " + board
        build_args += "  " + os.path.relpath(app_dir)
        build_args += "  " + app_cfg_p
        if libname is not None:
            build_args += "  " + libname
        print("\n bulid cmd:%s \n\n" %(build_args))
        ret = os.system(build_args)
        if ret != 0:
            print("\n bulid error\n")
            sys.exit(1)
        print("\n Warning: please build using Keil-MDK! \n\n")
        if silent == False:
            mdk_dir = os.path.join(out_dir, "mdk_clang")
            os.startfile(os.path.join(mdk_dir, "mdk_clang.uvprojx"))
        sys.exit(0)
    else:
        print("\n Error: please build on windows! \n\n")
        sys.exit(1)

# 构造 west build 命令，显式传入板名、应用目录和输出目录。west 通过 CMake 和 Ninja 编译、链接，生成 zephyr.elf、zephyr.map 和 zephyr.bin。
def build_zephyr_app_by_gcc(board, out_dir, app_dir, app_cfg_p, libname, silent):
    build_args = "west build -p auto -b " + board
    build_args += " -d " + out_dir + "  " + app_dir
    # 只有选中的配置子目录实际提供 prj.conf 时，才通过 -DCONF_FILE 显式指定；其余情况沿用工程配置搜索规则。
    if app_cfg_p != ".":
        cfg_file = os.path.join(app_cfg_p, "prj.conf")
        if os.path.exists(os.path.join(app_dir,cfg_file)) == True:
            if (is_windows()):
                cfg_file = cfg_file.replace('\\', '/')
            build_args += " -- " + " -DCONF_FILE=" + cfg_file

    # 在 SDK 的 zephyr 工作目录执行，编译失败时终止脚本，避免进入常规打包路径。
    os.chdir(sdk_root)
    print("\n bulid cmd:%s \n\n" %(build_args))
    ret = os.system(build_args)
    if ret != 0:
        print("\n bulid error\n")
        sys.exit(1)


# 对已有 out_dir 调用 west 的 menuconfig 目标，交互查看或调整该次构建的 Kconfig 配置。
def build_zephyr_menuconfig(out_dir):
    build_args = "west build -d " + out_dir
    build_args += " -t menuconfig"
    print("\n bulid cmd:%s \n\n" %(build_args))
    ret = os.system(build_args)
    if ret != 0:
        print("\n bulid error\n")
        sys.exit(1)

# 解析调用参数，选择板型、应用和配置，然后按模式执行清理、配置、编译或仅打包。argparse 默认使用当前进程的命令行。
def main(argv):
    parser = argparse.ArgumentParser()
    # -n 删除保存的选择，-c 清理该应用的 outdir，-m 打开 menuconfig，-p 使用已有编译产物仅打包。
    parser.add_argument('-n', dest="cfgdel", help="remove cur build_config", action="store_true", required=False)
    parser.add_argument('-c', dest="clear", help="remove board out dir and build_config", action="store_true", required=False)
    parser.add_argument('-m', dest='menuconfig', help="do sdk Configuration", action="store_true", default=False)
    parser.add_argument('-p', dest='pack', help="pack firmware only", action="store_true", default=False)
    # -l 为 Keil 路径附带库名，-s 在该路径阻止自动打开 IDE；已有 .build_config 时默认 GCC 路径直接复用选择。
    parser.add_argument('-l', dest="libname", help="build lib", required=False)
    parser.add_argument('-s', dest="silent", help="silent mode", action="store_true", required=False)
    # -t、-u 分别切换到 sample 和 test 目录，-k 选择 Keil 工程生成路径。
    parser.add_argument('-t', dest="sample", help="build sample, default build applicaction", action="store_true", required=False)
    parser.add_argument('-u', dest="test", help="build test, default build applicaction", action="store_true", required=False)
    parser.add_argument('-k', dest="bkeil", help="build by keil, default by gcc ", action="store_true", required=False)

    print("build")
    args = parser.parse_args()

    if args.sample == True:
        sdk_application_dir = sdk_samples_dir
    elif args.test == True:
        sdk_application_dir = sdk_tests_dir
    else:
        sdk_application_dir = sdk_app_dir

    application = ""
    sub_app = ""
    board_conf = ""
    app_conf  = ""

    if args.cfgdel == True:
        if os.path.exists(build_config_file) == True:
            os.remove(build_config_file)
            #sys.exit(0)

    # 从上次保存的选择中还原四个字段，确定要构建哪个应用、板型和配置。
    if os.path.exists(build_config_file) == True:
        fp = open(build_config_file,"r")
        lines = fp.readlines()
        for elem in lines:
            if elem[0] != '#':
                _c = elem.strip().split("=")
                if _c[0] == "APPLICATION":
                    application = _c[1]
                elif _c[0] == "BOARD":
                    board_conf  = _c[1]
                elif _c[0] == "SUB_APP":
                    sub_app = _c[1]
                elif _c[0] == "APP_CONF":
                    app_conf = _c[1]
        fp.close


    # 首次运行或使用 -n 后需要选目录。应用根目录缺少 CMakeLists.txt 时继续选择子应用。
    if os.path.exists(build_config_file) == False:
        board_conf = read_choice("Select boards configuration:", sdk_boards_dir)
        application = read_choice("Select application:", sdk_application_dir)
        sub_app = ""
        app_cfg_dir=os.path.join(sdk_application_dir, application)
        if os.path.exists(os.path.join(app_cfg_dir, "CMakeLists.txt")) == False:
            sub_app = read_choice("Select "+ application+" sub app:", os.path.join(sdk_application_dir, application))
            app_cfg_dir=os.path.join(app_cfg_dir, sub_app)
        if os.path.exists(os.path.join(app_cfg_dir, "CMakeLists.txt")) == False:
            print("application  %s not have CMakeLists.txt\n\n" %(app_cfg_dir))
            sys.exit(0)
        app_conf = ""
        if os.path.exists(os.path.join(app_cfg_dir, "app_conf")) == True:
            app_conf = read_choice("Select application conf:", os.path.join(app_cfg_dir, "app_conf"))
            app_cfg_dir = os.path.join(app_cfg_dir, "app_conf", app_conf);

        # 将本次交互选择写入 zephyr/.build_config，下次运行可复用。
        fp = open(build_config_file,"w")
        fp.write("# Build config\n")
        fp.write("BOARD=" + board_conf + "\n")
        fp.write("APPLICATION=" + application + "\n")
        fp.write("SUB_APP=" + sub_app + "\n")
        fp.write("APP_CONF=" + app_conf + "\n")
        fp.close()


    print("\n--== Build application %s (sub %s) (app_conf %s)  board %s ==--\n\n"  %(application, sub_app, app_conf,
        board_conf))


    # 根据应用根目录有无 CMakeLists.txt 决定传给 west 的应用源码路径。
    if os.path.exists(os.path.join(sdk_application_dir, application, "CMakeLists.txt")) == True:
        application_path = os.path.join(sdk_application_dir, application)
    else:
        application_path = os.path.join(sdk_application_dir, application, sub_app)

    if os.path.exists(application_path) == False:
        print("\nNo application at %s \n\n" %(sdk_application_dir))
        sys.exit(1)

    # 优先选 app_conf 下的命名配置，其次取按板型命名的 boards 子目录，最后回到应用根目录。
    if os.path.exists(os.path.join(application_path,  "app_conf")) == True:
        application_cfg_reldir=os.path.join("app_conf", app_conf)
    elif os.path.exists(os.path.join(application_path,  "boards")) == True:
        application_cfg_reldir =os.path.join("boards", board_conf)
    else :
        application_cfg_reldir = "."

    print(" application cfg dir %s, \n\n" %(application_cfg_reldir))

    # outdir 属于所选应用，每个板型在其下有独立输出目录。-c 在这里清理整个应用的 outdir。
    sdk_out = os.path.join(application_path, "outdir")
    sdk_build = os.path.join(sdk_out, board_conf)
    if args.clear == True:
        print("\n Clean build out=%s \n\n"  %(sdk_out))
        if os.path.exists(sdk_out) == True:
            shutil.rmtree(sdk_out, True)
            time.sleep(0.01)
        sys.exit(0)

    # menuconfig 使用已有板型构建目录，此分支结束后不继续打包。
    if args.menuconfig == True:
        if os.path.exists(sdk_build) == True:
            build_zephyr_menuconfig(sdk_build)
        else:
            print("please build")
        sys.exit(1)

    print("\n sdk_build=%s \n\n"  %(sdk_build))
    if os.path.exists(sdk_build) == False:
        os.makedirs(sdk_build)


    # 板级文件位于 zephyr/boards/arm 下；打包还要从这里取得当前板型的预置输入。
    board_conf_path = os.path.join(sdk_boards_dir, board_conf)
    if os.path.exists(board_conf_path) == False:
        print("\nNo board at %s \n\n" %(board_conf_dir))
        sys.exit(1)

    #build_zephyr_app(board_conf, sdk_build_boot, sdk_mcuboot_app_path)
    # -p 跳过编译步骤；默认 GCC 路径在 west 成功返回后继续打包。
    if args.pack == False:
        if args.bkeil == True:
            build_zephyr_app_by_keil(board_conf, sdk_build, application_path, application_cfg_reldir, args.libname, args.silent)
        else:
            build_zephyr_app_by_gcc(board_conf, sdk_build, application_path, application_cfg_reldir, args.libname, args.silent)

    # 使用同一应用、板型及输出目录，保证打包函数从这一轮指定的位置取输入。
    build_firmware(board_conf, sdk_build, board_conf_path, application_path, os.path.join(application_path, application_cfg_reldir))
    print("build finished")


# 脚本直接启动时进入 main；作为模块被导入时仅提供上述工具函数。
if __name__ == "__main__":
    main(sys.argv)
