"""
Build a multi-resolution ICO from PNG files.
Usage: python make_ico.py <prefix> <output.ico>
Expects: <prefix>_16.png, _24.png, _32.png, _48.png, _64.png, _128.png, _256.png
"""
import struct, sys, os

def build_ico(prefix, out_path):
    sizes = [16, 24, 32, 48, 64, 128, 256]
    pngs = []
    for s in sizes:
        p = f"{prefix}_{s}.png"
        with open(p, 'rb') as f:
            data = f.read()
        pngs.append((s, data))

    # ICO header: reserved(2) + type(2) + count(2)
    header = struct.pack('<HHH', 0, 1, len(pngs))

    # Directory entries: 16 bytes each
    # width(1) height(1) colorCount(1) reserved(1) planes(2) bitCount(2) bytesInRes(4) imageOffset(4)
    dir_size   = 16 * len(pngs)
    img_offset = 6 + dir_size

    directory = b''
    for (size, data) in pngs:
        w = h = size if size < 256 else 0   # 0 means 256 in ICO format
        directory += struct.pack('<BBBBHHII',
            w, h, 0, 0, 1, 32,
            len(data), img_offset)
        img_offset += len(data)

    with open(out_path, 'wb') as f:
        f.write(header)
        f.write(directory)
        for (_, data) in pngs:
            f.write(data)

    print(f"  wrote {out_path}  ({os.path.getsize(out_path)} bytes)")

if __name__ == '__main__':
    if len(sys.argv) != 3:
        print("Usage: python make_ico.py <prefix> <output.ico>")
        sys.exit(1)
    build_ico(sys.argv[1], sys.argv[2])
