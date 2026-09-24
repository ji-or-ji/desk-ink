#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
badapple.py - Bad Apple 墨水屏帧序列转换工具

把视频转成固件可直接播放的 BAF1 帧包：

    视频 -> ffmpeg 抽帧(灰度 8bit) -> 4x4 Bayer 抖动 -> 1-bit 打包
         -> PackBits RLE -> .baf 容器 (+ manifest.json + 预览 PNG)

设计约定（与 tools/render-preview.ps1、固件 eink.c 完全一致）:
    位图行优先，每行 ceil(width/8) 字节，MSB 在左，1 = 黑，0 = 白。
    抖动判定: 灰度 < (Bayer值 + 0.5) / 16 * 255 时取黑。
    400x300 时一帧裸数据 50 * 300 = 15000 字节。

BAF1 容器（全部小端）:
    头部 16 字节:
        0   char[4]  magic       "BAF1"
        4   u8       version     1
        5   u8       rle_id      1 = PackBits
        6   u8       flags       保留，当前 0
        7   u8       reserved    0
        8   u16      width
        10  u16      height
        12  u16      fps_milli   帧率 x 1000
        14  u16      frame_count
    索引 frame_count * 8 字节:
        u32 offset   帧记录的绝对文件偏移
        u32 length   帧记录总长（含 8 字节帧头）
    帧记录:
        u16 dirty_x, dirty_y, dirty_w, dirty_h   相对上一帧的变化区域
        u8  payload[]                            PackBits 压缩后的位图
    首帧的 dirty 区域为整屏。

PackBits 控制字节 c（固件解码按同一约定）:
    0..127    后接 c+1 个原样字节
    129..255  后接 1 个字节，重复 257-c 次
    128       空操作

命令:
    convert   视频 -> .baf + manifest + 预览
    decode    .baf -> PNG（用于肉眼比对）
    info      打印 .baf 头部与压缩统计
    selftest  不依赖 ffmpeg 的内部往返自测
"""

import argparse
import datetime
import json
import os
import shutil
import struct
import subprocess
import sys
import zlib
from pathlib import Path

for _stream in (sys.stdout, sys.stderr):
    try:
        _stream.reconfigure(encoding="utf-8")
    except (AttributeError, ValueError):
        pass

MAGIC = b"BAF1"
VERSION = 1
RLE_PACKBITS = 1
HEADER_SIZE = 16
INDEX_ENTRY_SIZE = 8
FRAME_HEADER_SIZE = 8

BAYER = ((0, 8, 2, 10), (12, 4, 14, 6), (3, 11, 1, 9), (15, 7, 13, 5))
TO_ASCII = bytes.maketrans(bytes((0, 1)), b"01")


def _make_black_tables():
    """BLACK[row][col_phase][gray] -> 0/1，供 bytes.translate 使用。"""
    tables = []
    for row in BAYER:
        row_tables = []
        for value in row:
            threshold = (value + 0.5) / 16.0 * 255.0
            row_tables.append(bytes(1 if g < threshold else 0 for g in range(256)))
        tables.append(row_tables)
    return tables


BLACK = _make_black_tables()


# ---------------------------------------------------------------- 抖动与打包

def dither_pack(gray, width, height, threshold=None):
    """灰度字节 -> 1-bit 位图（1 = 黑），MSB 在左。threshold 非空则走固定阈值。"""
    stride = (width + 7) // 8
    pad = stride * 8 - width
    out = bytearray(stride * height)
    offset = 0

    if threshold is not None:
        thr = int(threshold)
        table = bytes(1 if g < thr else 0 for g in range(256))
        for y in range(height):
            row = gray[y * width:(y + 1) * width]
            bits = row.translate(table)
            value = int(bits.translate(TO_ASCII), 2)
            if pad:
                value <<= pad
            out[offset:offset + stride] = value.to_bytes(stride, "big")
            offset += stride
        return bytes(out)

    for y in range(height):
        row = gray[y * width:(y + 1) * width]
        tables = BLACK[y & 3]
        bits = bytearray(width)
        bits[0::4] = row[0::4].translate(tables[0])
        bits[1::4] = row[1::4].translate(tables[1])
        bits[2::4] = row[2::4].translate(tables[2])
        bits[3::4] = row[3::4].translate(tables[3])
        value = int(bits.translate(TO_ASCII), 2)
        if pad:
            value <<= pad
        out[offset:offset + stride] = value.to_bytes(stride, "big")
        offset += stride
    return bytes(out)


def packed_to_gray(packed, width, height):
    """1-bit 位图 -> 灰度字节（黑 0 / 白 255），供预览与比对。"""
    stride = (width + 7) // 8
    out = bytearray(width * height)
    for y in range(height):
        row_offset = y * stride
        out_offset = y * width
        for x in range(width):
            bit = (packed[row_offset + (x >> 3)] >> (7 - (x & 7))) & 1
            out[out_offset + x] = 0 if bit else 255
    return bytes(out)


# ------------------------------------------------------------------ PackBits

def packbits_encode(data):
    out = bytearray()
    n = len(data)
    i = 0
    while i < n:
        run = 1
        while i + run < n and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            out.append(257 - run)
            out.append(data[i])
            i += run
            continue
        start = i
        i += 1
        while i < n and i - start < 128:
            if i + 2 < n and data[i] == data[i + 1] == data[i + 2]:
                break
            i += 1
        out.append(i - start - 1)
        out.extend(data[start:i])
    return bytes(out)


def packbits_decode(data, raw_len):
    out = bytearray()
    i = 0
    n = len(data)
    while i < n and len(out) < raw_len:
        control = data[i]
        i += 1
        if control < 128:
            count = control + 1
            out.extend(data[i:i + count])
            i += count
        elif control > 128:
            count = 257 - control
            if i < n:
                out.extend(bytes((data[i],)) * count)
                i += 1
    return bytes(out)


# ---------------------------------------------------------------- 变化区域

def dirty_rect(prev, cur, width, height):
    """两帧 1-bit 位图的差异包围盒，x 对齐到字节边界。相同则返回 (0,0,0,0)。"""
    stride = (width + 7) // 8
    x0, y0, x1, y1 = width, height, -1, -1
    for y in range(height):
        row_offset = y * stride
        a = prev[row_offset:row_offset + stride]
        b = cur[row_offset:row_offset + stride]
        if a == b:
            continue
        if y < y0:
            y0 = y
        y1 = y
        first = last = -1
        for bi in range(stride):
            if a[bi] != b[bi]:
                if first < 0:
                    first = bi
                last = bi
        diff = a[first] ^ b[first]
        left = first * 8 + (7 - (diff.bit_length() - 1))
        diff = a[last] ^ b[last]
        right = last * 8 + (7 - ((diff & -diff).bit_length() - 1))
        if left < x0:
            x0 = left
        if right > x1:
            x1 = right
    if x1 < 0:
        return (0, 0, 0, 0)
    x0 = (x0 // 8) * 8
    x1 = min(width - 1, ((x1 // 8) + 1) * 8 - 1)
    return (x0, y0, x1 - x0 + 1, y1 - y0 + 1)


# ---------------------------------------------------------------- 容器读写

def write_bundle(path, width, height, fps, frames):
    count = len(frames)
    header = struct.pack(
        "<4sBBBBHHHH",
        MAGIC, VERSION, RLE_PACKBITS, 0, 0,
        width, height, int(round(fps * 1000)), count,
    )
    base = HEADER_SIZE + count * INDEX_ENTRY_SIZE
    entries = []
    for _, payload in frames:
        length = FRAME_HEADER_SIZE + len(payload)
        entries.append((base, length))
        base += length

    with open(path, "wb") as handle:
        handle.write(header)
        for offset, length in entries:
            handle.write(struct.pack("<II", offset, length))
        for (dirty, payload) in frames:
            handle.write(struct.pack("<HHHH", *dirty))
            handle.write(payload)


def read_bundle(path):
    blob = Path(path).read_bytes()
    if len(blob) < HEADER_SIZE:
        raise ValueError("文件太短，不是 BAF1: %s" % path)
    magic, version, rle_id, flags, reserved, width, height, fps_milli, count = \
        struct.unpack_from("<4sBBBBHHHH", blob, 0)
    if magic != MAGIC:
        raise ValueError("magic 不对: %r" % magic)
    if version != VERSION:
        raise ValueError("版本不支持: %d" % version)
    meta = {
        "version": version,
        "rle_id": rle_id,
        "flags": flags,
        "width": width,
        "height": height,
        "fps": fps_milli / 1000.0,
        "frame_count": count,
        "stride": (width + 7) // 8,
    }
    frames = []
    for i in range(count):
        offset, length = struct.unpack_from("<II", blob, HEADER_SIZE + i * INDEX_ENTRY_SIZE)
        dirty = struct.unpack_from("<HHHH", blob, offset)
        payload = blob[offset + FRAME_HEADER_SIZE:offset + length]
        frames.append((dirty, payload))
    return meta, frames


# ---------------------------------------------------------------- PNG 输出

def _scale_gray(pixels, width, height, scale):
    if scale <= 1:
        return pixels, width, height
    scaled_width = width * scale
    out = bytearray(scaled_width * height * scale)
    for y in range(height):
        src = pixels[y * width:(y + 1) * width]
        line = bytearray()
        for value in src:
            line.extend((value,) * scale)
        for sy in range(scale):
            start = (y * scale + sy) * scaled_width
            out[start:start + scaled_width] = line
    return bytes(out), scaled_width, height * scale


def write_png(path, width, height, pixels):
    raw = bytearray()
    for y in range(height):
        raw.append(0)
        raw.extend(pixels[y * width:(y + 1) * width])
    compressed = zlib.compress(bytes(raw), 6)

    def chunk(tag, data):
        return (struct.pack(">I", len(data)) + tag + data
                + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 0, 0, 0, 0))
    png += chunk(b"IDAT", compressed)
    png += chunk(b"IEND", b"")
    with open(path, "wb") as handle:
        handle.write(png)


def save_frame_png(path, packed, width, height, scale):
    gray = packed_to_gray(packed, width, height)
    gray, out_w, out_h = _scale_gray(gray, width, height, scale)
    write_png(path, out_w, out_h, gray)


# ---------------------------------------------------------------- ffmpeg 抽帧

def _read_exact(stream, size):
    buf = bytearray()
    while len(buf) < size:
        chunk = stream.read(size - len(buf))
        if not chunk:
            return None
        buf.extend(chunk)
    return bytes(buf)


def iter_gray_frames(video, fps, width, height, start, duration, ffmpeg, limit=0):
    cmd = [ffmpeg, "-hide_banner", "-loglevel", "error", "-nostdin"]
    if start:
        cmd += ["-ss", str(start)]
    cmd += ["-i", video]
    if duration:
        cmd += ["-t", str(duration)]
    vf = ("fps=%s,scale=%d:%d:force_original_aspect_ratio=increase:"
          "flags=bicubic,crop=%d:%d" % (fps, width, height, width, height))
    cmd += ["-an", "-vf", vf]
    if limit:
        cmd += ["-frames:v", str(limit)]
    cmd += ["-pix_fmt", "gray", "-f", "rawvideo", "-"]

    proc = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    frame_bytes = width * height
    try:
        while True:
            data = _read_exact(proc.stdout, frame_bytes)
            if data is None:
                break
            yield data
    finally:
        proc.stdout.close()
        stderr = proc.stderr.read()
        proc.stderr.close()
        code = proc.wait()
        if code != 0:
            raise RuntimeError("ffmpeg 退出码 %d:\n%s" % (code, stderr.decode("utf-8", "replace")))


# ------------------------------------------------------------------- 子命令

def cmd_convert(args):
    if not os.path.isfile(args.video):
        raise SystemExit("找不到视频: %s" % args.video)
    ffmpeg = args.ffmpeg if os.path.isfile(args.ffmpeg) else shutil.which(args.ffmpeg)
    if not ffmpeg:
        raise SystemExit("找不到 ffmpeg: %s" % args.ffmpeg)

    out_bundle = Path(args.out)
    out_bundle.parent.mkdir(parents=True, exist_ok=True)

    frames = []
    payload_sizes = []
    previous = None
    for gray in iter_gray_frames(args.video, args.fps, args.width, args.height,
                                args.start, args.duration, ffmpeg, args.limit):
        packed = dither_pack(gray, args.width, args.height,
                             threshold=args.threshold if args.no_dither else None)
        if previous is None:
            dirty = (0, 0, args.width, args.height)
        else:
            dirty = dirty_rect(previous, packed, args.width, args.height)
        payload = packbits_encode(packed)
        frames.append((dirty, payload))
        payload_sizes.append(len(payload))
        previous = packed

    if not frames:
        raise SystemExit("没有抽到任何帧，检查 -ss/-t/fps 参数")

    write_bundle(out_bundle, args.width, args.height, args.fps, frames)

    raw_frame = ((args.width + 7) // 8) * args.height
    raw_total = raw_frame * len(frames)
    bundle_bytes = out_bundle.stat().st_size

    preview_dir = out_bundle.parent / "preview"
    if args.previews > 0:
        preview_dir.mkdir(parents=True, exist_ok=True)
        count = min(args.previews, len(frames))
        for i in range(count):
            index = 0 if count == 1 else round(i * (len(frames) - 1) / (count - 1))
            _, payload = frames[index]
            packed = packbits_decode(payload, raw_frame)
            save_frame_png(preview_dir / ("frame-%05d.png" % index),
                           packed, args.width, args.height, args.scale)

    manifest = {
        "format": "BAF1",
        "version": VERSION,
        "rle": "packbits",
        "dither": "none" if args.no_dither else "bayer4x4",
        "source": os.path.abspath(args.video),
        "width": args.width,
        "height": args.height,
        "stride": (args.width + 7) // 8,
        "fps": args.fps,
        "start": args.start,
        "duration": args.duration,
        "frame_count": len(frames),
        "raw_frame_bytes": raw_frame,
        "raw_total_bytes": raw_total,
        "bundle_bytes": bundle_bytes,
        "compression_ratio": (raw_total / bundle_bytes) if bundle_bytes else 0,
        "payload_bytes_min": min(payload_sizes),
        "payload_bytes_avg": sum(payload_sizes) / len(payload_sizes),
        "payload_bytes_max": max(payload_sizes),
        "created": datetime.datetime.now().isoformat(timespec="seconds"),
        "bundle": out_bundle.name,
    }
    manifest_path = out_bundle.with_suffix(".manifest.json")
    manifest_path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2), encoding="utf-8")

    print("帧数      : %d" % len(frames))
    print("裸数据    : %d 字节 (%.2f MB)" % (raw_total, raw_total / 1048576.0))
    print("帧包      : %s (%d 字节, %.2f MB)" % (out_bundle, bundle_bytes, bundle_bytes / 1048576.0))
    print("压缩比    : %.2fx" % manifest["compression_ratio"])
    print("单帧载荷  : min %d / avg %.0f / max %d 字节"
          % (manifest["payload_bytes_min"], manifest["payload_bytes_avg"],
             manifest["payload_bytes_max"]))
    if args.previews > 0:
        print("预览      : %s" % preview_dir)
    print("manifest  : %s" % manifest_path)


def cmd_decode(args):
    meta, frames = read_bundle(args.inp)
    raw_frame = meta["stride"] * meta["height"]
    out_dir = Path(args.outdir)
    out_dir.mkdir(parents=True, exist_ok=True)

    selected = list(range(len(frames)))
    if args.every > 1:
        selected = selected[::args.every]
    if args.limit:
        selected = selected[:args.limit]

    for index in selected:
        _, payload = frames[index]
        packed = packbits_decode(payload, raw_frame)
        if len(packed) != raw_frame:
            print("警告: 第 %d 帧解出 %d 字节，应为 %d"
                  % (index, len(packed), raw_frame), file=sys.stderr)
        save_frame_png(out_dir / ("frame-%05d.png" % index),
                       packed, meta["width"], meta["height"], args.scale)
    print("输出 %d 帧 -> %s (scale=%d)" % (len(selected), out_dir, args.scale))


def cmd_info(args):
    meta, frames = read_bundle(args.inp)
    raw_frame = meta["stride"] * meta["height"]
    sizes = [len(payload) for _, payload in frames]
    raw_total = raw_frame * len(frames)
    bundle_bytes = Path(args.inp).stat().st_size
    print("BAF1 version=%d rle=%d flags=%d" % (meta["version"], meta["rle_id"], meta["flags"]))
    print("尺寸      : %dx%d stride=%d" % (meta["width"], meta["height"], meta["stride"]))
    print("帧率      : %.3f fps" % meta["fps"])
    print("帧数      : %d" % meta["frame_count"])
    print("裸数据    : %d 字节" % raw_total)
    print("帧包      : %d 字节" % bundle_bytes)
    print("压缩比    : %.2fx" % (raw_total / bundle_bytes if bundle_bytes else 0))
    if sizes:
        print("单帧载荷  : min %d / avg %.0f / max %d 字节"
              % (min(sizes), sum(sizes) / len(sizes), max(sizes)))


def _naive_dither_pack(gray, width, height):
    """逐像素参考实现，用来验证 dither_pack 的 translate 优化。"""
    stride = (width + 7) // 8
    out = bytearray(stride * height)
    for y in range(height):
        for x in range(width):
            threshold = (BAYER[y & 3][x & 3] + 0.5) / 16.0 * 255.0
            if gray[y * width + x] < threshold:
                out[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    return bytes(out)


def cmd_selftest(_args):
    import random

    random.seed(1234)
    checks = []

    for _ in range(50):
        data = bytes(random.randrange(256) for _ in range(random.randrange(0, 2000)))
        decoded = packbits_decode(packbits_encode(data), len(data))
        checks.append(decoded == data)

    for value in (0x00, 0xFF, 0xAA, 0x55):
        data = bytes((value,)) * 5000
        decoded = packbits_decode(packbits_encode(data), len(data))
        checks.append(decoded == data)

    width, height = 400, 300
    stride = (width + 7) // 8
    gray = bytes((x * 255 // (width - 1)) for _ in range(height) for x in range(width))
    packed = dither_pack(gray, width, height)
    checks.append(len(packed) == stride * height)
    back = packed_to_gray(packed, width, height)
    checks.append(all(v in (0, 255) for v in back))

    for (tw, th) in ((37, 11), (134, 200), (400, 300)):
        tstride = (tw + 7) // 8
        tgray = bytes(random.randrange(256) for _ in range(tw * th))
        checks.append(dither_pack(tgray, tw, th) == _naive_dither_pack(tgray, tw, th))

    base = bytes(stride * height)
    changed = bytearray(base)
    for y in range(5, 9):
        for x in range(10, 21):
            changed[y * stride + (x >> 3)] |= 0x80 >> (x & 7)
    dx, dy, dw, dh = dirty_rect(base, bytes(changed), width, height)
    checks.append(dx == 8 and dy == 5 and dx + dw > 20 and dy + dh > 8)
    checks.append(dirty_rect(base, base, width, height) == (0, 0, 0, 0))

    frames = [
        ((0, 0, width, height), packbits_encode(packed)),
        ((0, 0, width, height), packbits_encode(bytes(stride * height))),
    ]
    tmp = Path(__file__).resolve().parent / "out" / "_selftest.baf"
    tmp.parent.mkdir(parents=True, exist_ok=True)
    write_bundle(tmp, width, height, 2, frames)
    meta, read_frames = read_bundle(tmp)
    checks.append(meta["frame_count"] == 2 and meta["fps"] == 2.0)
    checks.append(read_frames[0][1] == frames[0][1] and read_frames[1][1] == frames[1][1])
    checks.append(packbits_decode(read_frames[1][1], stride * height) == bytes(stride * height))
    tmp.unlink()

    if all(checks):
        print("selftest PASS (%d 项)" % len(checks))
        return 0
    print("selftest FAIL", file=sys.stderr)
    return 1


# ---------------------------------------------------------------------- CLI

def build_parser():
    parser = argparse.ArgumentParser(
        description="Bad Apple 墨水屏帧序列转换工具（BAF1 + PackBits）")
    sub = parser.add_subparsers(dest="command", required=True)

    convert = sub.add_parser("convert", help="视频 -> .baf + manifest + 预览")
    convert.add_argument("video", help="输入视频路径")
    convert.add_argument("--fps", type=float, required=True, help="目标帧率，必填")
    convert.add_argument("--width", type=int, default=400)
    convert.add_argument("--height", type=int, default=300)
    convert.add_argument("--start", type=float, default=0, help="起始秒数")
    convert.add_argument("--duration", type=float, default=None, help="截取时长（秒）")
    convert.add_argument("--limit", type=int, default=0, help="最多转换多少帧，0 表示不限")
    convert.add_argument("--out", default=str(Path(__file__).resolve().parent / "out" / "badapple.baf"))
    convert.add_argument("--previews", type=int, default=6, help="均匀导出的预览帧数")
    convert.add_argument("--scale", type=int, default=2, help="预览放大倍数")
    convert.add_argument("--no-dither", action="store_true", help="关闭抖动，改用固定阈值")
    convert.add_argument("--threshold", type=int, default=128)
    convert.add_argument("--ffmpeg", default="ffmpeg")
    convert.set_defaults(func=cmd_convert)

    decode = sub.add_parser("decode", help=".baf -> PNG")
    decode.add_argument("--in", dest="inp", required=True)
    decode.add_argument("--outdir", default=None)
    decode.add_argument("--every", type=int, default=1, help="每 N 帧导一张")
    decode.add_argument("--limit", type=int, default=0)
    decode.add_argument("--scale", type=int, default=2)
    decode.set_defaults(func=cmd_decode)

    info = sub.add_parser("info", help="打印帧包信息")
    info.add_argument("--in", dest="inp", required=True)
    info.set_defaults(func=cmd_info)

    selftest = sub.add_parser("selftest", help="内部往返自测")
    selftest.set_defaults(func=cmd_selftest)

    return parser


def main(argv=None):
    parser = build_parser()
    args = parser.parse_args(argv)
    if args.command == "decode" and not args.outdir:
        args.outdir = str(Path(args.inp).resolve().parent / "decoded")
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
