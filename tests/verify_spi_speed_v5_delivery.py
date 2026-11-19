"""Verify both V5 packages, record hashes, and preserve this session's changes."""
from pathlib import Path
import argparse
import difflib
import hashlib
import json
import re
import shutil
import struct
import sys
import tempfile
import zipfile
import zlib

deps = Path(tempfile.gettempdir()) / "uma503_dma_validation_testdeps"
if deps.is_dir():
    sys.path.insert(0, str(deps))
from elftools.elf.elffile import ELFFile


def sha(data):
    return hashlib.sha256(data).hexdigest()


def symbols(path):
    with path.open("rb") as f:
        elf = ELFFile(f)
        return {s.name: {"address": s["st_value"], "size": s["st_size"]}
                for s in elf.get_section_by_name(".symtab").iter_symbols()}


def source_snapshot(root, out, extra):
    baseline = json.loads((out / "before_hashes.json").read_text(encoding="utf-8"))
    paths = [i["path"] for i in baseline] + extra
    hashes, patches, changed = {}, [], []
    for rel in paths:
        p = root / rel
        dst = out / "source" / rel
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(p, dst)
        hashes[rel] = sha(p.read_bytes())
        before = out / "before" / rel
        previous = before.read_text(encoding="utf-8") if before.exists() else ""
        current = p.read_text(encoding="utf-8")
        if previous != current:
            changed.append(rel)
            patches.extend(difflib.unified_diff(previous.splitlines(True), current.splitlines(True),
                                                fromfile="before/" + rel, tofile="v5/" + rel))
    (out / "this_session.patch").write_text("".join(patches), encoding="utf-8")
    return hashes, changed


def normalize(commands):
    return [{k: v for k, v in c.items() if k not in
             (("weight", "value") if c.get("command") == "progress" else ("weight",))}
            for c in commands]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--uma-root", type=Path, default=Path("D:/UMA503/UMA503-V7_OPENCPU_OC_2025.03.14_1644"))
    args = parser.parse_args()
    ats = Path(__file__).resolve().parents[1]
    uma = args.uma_root.resolve()
    tag = ats / "TAG_ZS3085E_241230"
    build = tag / "application/bt_watch_no_psram/outdir/ats3085e_dev_watch_ext_nor"
    ao, uo = [r / "artifacts/spi_speed_v5_20261118" for r in (ats, uma)]
    alog = (ao / "build.log").read_text(encoding="utf-16")
    ulog = (uma / "startup/app_release/app_buildlog.log").read_text(errors="replace")
    assert "build finished" in alog and "GCC Compiling Finished Sucessfully" in ulog
    for log in (alog, ulog):
        assert not re.search(r"error:|undefined reference|FAILED:|CovertFWFormat fail|Out of memory", log, re.I)
    warnings = [line for line in ulog.splitlines() if "warning:" in line]
    assert not any("demo_spi.c" in line or "spi_link_dma" in line for line in warnings)
    (uo / "sdk_build_warnings.txt").write_text("\n".join(warnings) + "\n", encoding="utf-8")
    tests = []
    for out in (ao, uo):
        log = (out / "tests_run.log").read_text(encoding="utf-8")
        match = re.search(r"Ran (\d+) tests", log)
        assert match and re.search(r"\nOK\s*$", log)
        tests.append(int(match.group(1)))
    assert tests[0] >= 10 and tests[1] >= 42

    # Protocol has unchanged wire version/length and identical source on both ends.
    protocol = "application/bt_watch_no_psram/src/main/spi_link_swap.h"
    assert (tag / protocol).read_bytes() == (uma / "startup/app_release/demo/spi_link_swap.h").read_bytes()
    p = (tag / protocol).read_text(encoding="utf-8")
    assert "#define SWAP_VER 3u" in p and "#define SWAP_SIZE 256u" in p

    # Known-good historical packages and unrelated source remain byte-identical.
    old = uma / "artifacts/UMA503_SPI_DMA_VALIDATION_V4_ABOOT_20261117.zip"
    assert sha(old.read_bytes()) == "1d5919deb4625d14c86f0fb9fe8ba7718946832395882f7f9a3810796f34e865"
    old_ats = build / "_firmware/ATS3085E_SPI2_hardware_burst_length_20261117.fw"
    assert sha(old_ats.read_bytes()) == "f04256daea5ae9c1ec915e37621d4ee4be51e5c720bd25fe5250ac6578dcab9b"
    for root, out, paths in [
        (ats, ao, ["TAG_ZS3085E_241230/zephyr/soc/arm/actions/leopard/soc_clock.c",
                   "TAG_ZS3085E_241230/zephyr/soc/arm/actions/leopard/soc_clock.h",
                   "TAG_ZS3085E_241230/zephyr/boards/arm/ats3085e_dev_watch_ext_nor/board_cfg.h"]),
        (uma, uo, ["startup/app_release/demo/demo_spi.c", "startup/app_release/demo/spi_link_dma_config.h"])
    ]:
        for rel in paths:
            assert (root / rel).read_bytes() == (out / "before" / rel).read_bytes(), rel

    app = (uma / "startup/app_release/build/app.bin").read_bytes()
    assert b"SPI DMA VALIDATION v5" in app and b"SPI DMA READY v5" in app
    assert b"SPI DMA VALIDATION v4" not in app
    package = uma / "artifacts/UMA503_SPI_SPEED_V5_ABOOT_20261118.zip"
    with zipfile.ZipFile(package) as new, zipfile.ZipFile(old) as previous:
        assert new.testzip() is None
        assert new.read("app.bin") == app and set(new.namelist()) == set(previous.namelist())
        commands = json.loads(new.read("download.json"))
        assert normalize(commands) == normalize(json.loads(previous.read("download.json")))
        assert all(c["image"] in new.namelist() for c in commands if "image" in c)
        table, old_table = new.read("partition.bin"), previous.read("partition.bin")
        assert table[:4] == b"BTPA" and zlib.crc32(table[:-4]) == struct.unpack("<I", table[-4:])[0]
        index = table.index(b"app\0")
        assert struct.unpack_from("<I", table, index + 60)[0] == len(app)
        expected = bytearray(old_table)
        expected[index + 60:index + 64] = table[index + 60:index + 64]
        expected[-4:] = table[-4:]
        assert bytes(expected) == table
        assert new.read("system.img") == table + previous.read("system.img")[len(old_table):]
        assert previous.read("bootloader.ubi").count(old_table) == 1
        assert new.read("bootloader.ubi") == previous.read("bootloader.ubi").replace(old_table, table)
        regenerated = {"app.bin", "download.json", "partition.bin", "system.img", "bootloader.ubi", "firmware.bin"}
        assert all(new.read(n) == previous.read(n) for n in new.namelist() if n not in regenerated)
        firmware = new.read("firmware.bin")
        assert firmware[:8] == b"!CRANE! " and int(firmware[8:16], 16) == len(firmware)
        uma_members = {n: sha(new.read(n)) for n in new.namelist()}

    zephyr = (build / "zephyr/zephyr.bin").read_bytes()
    assert b"SPI SPEED v5" in zephyr and b"SPI2 DMA FIRST_OK" in zephyr and b"spi2swapdma" in zephyr
    fw = build / "_firmware/ats3085e_dev_watch_ext_nor_261118.fw"
    data = fw.read_bytes()
    assert data[:8] == b"ACTTEST0" and 0 < data[12] <= 15
    assert struct.unpack_from("<I", data, 8)[0] == zlib.crc32(data[12:512])
    assert struct.unpack_from("<I", data, 24)[0] == zlib.crc32(data[512:])
    assert struct.unpack_from("<I", data, 20)[0] == len(data)
    members = {}
    for n in range(data[12]):
        name, offset, size, crc = struct.unpack_from("<12s4xII4xI", data, 32 + n * 32)
        name = name.rstrip(b"\0").decode("ascii")
        assert offset >= 512 and offset + size <= len(data)
        payload = data[offset:offset + size]
        assert zlib.crc32(payload) == crc
        source = build / "_firmware/bin_orig/ATT" / name
        if not source.exists():
            source = build / "_firmware/bin_orig" / name
        assert source.read_bytes() == payload, name
        members[name] = sha(payload)
    # SDK post-processing changes the app header. Check executable body before
    # its proprietary inner container; ATF and inner-source identity checked above.
    packed_input = (build / "_firmware/bin_orig/app.bin").read_bytes()
    assert len(packed_input) == (len(zephyr) + 511) // 512 * 512
    assert packed_input[512:len(zephyr)] == zephyr[512:]
    assert not any(packed_input[len(zephyr):])
    assert packed_input[:8] == b"ACTHHTCA"
    image_size = struct.unpack_from("<I", packed_input, 0x18)[0]
    assert image_size == (len(zephyr) + 3) // 4 * 4 - 512
    image_sum = sum(struct.unpack("<%dI" % (image_size // 4), packed_input[512:512 + image_size]))
    assert (image_sum + struct.unpack_from("<I", packed_input, 0x1c)[0]) & 0xffffffff == 0xffffffff
    assert sum(struct.unpack("<128I", packed_input[:512])) & 0xffffffff == 0xffffffff
    final_fw = ats / "artifacts/ATS3085E_SPI_SPEED_V5_20261118.fw"
    shutil.copy2(fw, final_fw)
    assert final_fw.read_bytes() == data

    aelf, uelf = symbols(build / "zephyr/zephyr.elf"), symbols(uma / "startup/app_release/build/app.elf")
    for name in ("spi2_dma_tx", "spi2_dma_rx"):
        s = aelf[name]
        assert s["size"] == 256 and s["address"] % 4 == 0
        assert 0x2ff60400 <= s["address"] < s["address"] + 256 <= 0x2ff60400 + 911 * 1024
    for name in ("ld_tx", "ld_rx"):
        s = uelf[name]
        assert s["size"] == 384 and s["address"] % 64 == 0
        assert 0x7e040800 <= s["address"] < s["address"] + 384 <= 0x7e048800

    a_hashes, a_changes = source_snapshot(ats, ao, [
        "TAG_ZS3085E_241230/zephyr/drivers/spi/spi_acts_spi2_dma.h",
        "tests/test_spi2_dma_v5.py", "tests/verify_spi_speed_v5_delivery.py"])
    u_hashes, u_changes = source_snapshot(uma, uo, [])
    for root, out, binary, elf in [(ats, ao, build / "zephyr/zephyr.bin", build / "zephyr/zephyr.elf"),
                                   (uma, uo, uma / "startup/app_release/build/app.bin", uma / "startup/app_release/build/app.elf")]:
        shutil.copy2(binary, out / binary.name)
        shutil.copy2(elf, out / elf.name)
    manifest = {
        "date_local": "2026-11-18", "timezone": "Asia/Shanghai", "variant": "SPI SPEED V5",
        "hardware_tested": False, "measured_throughput_Bps": None,
        "test_counts": {"ats_arm_mmio": tests[0], "uma_arm_mmio_crc": tests[1]},
        "uma_sdk_warning_count": len(warnings), "spi_compile_warnings": 0,
        "ats_compile_warning_count": len(re.findall("warning:", alog)),
        "wire_protocol_version": 3, "frame_bytes": 256, "payload_bytes": 240,
        "initial_candidate": {"requested_hz": 2300000, "gap_ms": 10, "burst_gap_us": 0},
        "ats_package": {"path": str(final_fw), "bytes": len(data), "sha256": sha(data), "members": members},
        "uma_package": {"path": str(package), "bytes": package.stat().st_size,
                        "sha256": sha(package.read_bytes()), "members": uma_members},
        "ats_zephyr_sha256": sha(zephyr), "uma_app_sha256": sha(app),
        "ats_dma_buffers": {n: aelf[n] for n in ("spi2_dma_tx", "spi2_dma_rx")},
        "uma_dma_buffers": {n: uelf[n] for n in ("ld_tx", "ld_rx")},
        "ats_sources": a_hashes, "uma_sources": u_hashes,
        "ats_session_changes": a_changes, "uma_session_changes": u_changes,
        "ats_package_checks": "ATF CRCs, each member vs SDK output, inner container vs maker output; proprietary inner app not independently decoded",
        "v4_packages_preserved": True,
    }
    for out in (ao, uo):
        (out / "manifest.json").write_text(json.dumps(manifest, indent=2, ensure_ascii=False), encoding="utf-8")
        (out / "SHA256.txt").write_text("\n".join([
            sha(data) + "  " + str(final_fw), sha(package.read_bytes()) + "  " + str(package),
            sha(zephyr) + "  zephyr.bin", sha(app) + "  app.bin"]) + "\n", encoding="utf-8")
    print(json.dumps({"verified": True, "tests": sum(tests), "uma_warnings": len(warnings),
                      "ats_fw_bytes": len(data), "uma_app_bytes": len(app),
                      "hardware_tested": False}, indent=2))


if __name__ == "__main__":
    main()
