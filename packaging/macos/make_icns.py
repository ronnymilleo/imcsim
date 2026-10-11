"""Rasterizes packaging/imcsim.svg into packaging/macos/imcsim.icns, without third-party packages.

The drawing comes from packaging/windows/make_icon.py; rerun both scripts after changing it. The rounded square is
inset as on the macOS icon grid (824 of 1024 pixels), so the icon matches the size of the others in the Dock. Every
size is stored as PNG, which macOS reads in icons since 10.7.
"""

import pathlib
import struct
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent.parent / "windows"))
from make_icon import render  # noqa: E402

# The square spans 240 of the 256 units of the drawing, and 824 of 1024 pixels on the grid: 240 * 1024 / 824 = 298
# units in all, 21 on each side
Padding = 21
# The icon types of each size; the @2x types of a size share its image
Types = {
    16: [b"icp4"],
    32: [b"icp5", b"ic11"],
    64: [b"ic12"],
    128: [b"ic07"],
    256: [b"ic08", b"ic13"],
    512: [b"ic09", b"ic14"],
    1024: [b"ic10"],
}


def main():
    entries = b""
    for size, kinds in Types.items():
        image = render(size, Padding)
        for kind in kinds:
            entries += kind + struct.pack(">I", 8 + len(image)) + image
    output = pathlib.Path(__file__).with_name("imcsim.icns")
    output.write_bytes(b"icns" + struct.pack(">I", 8 + len(entries)) + entries)
    print(f"{output}: {len(Types)} sizes")


if __name__ == "__main__":
    main()
