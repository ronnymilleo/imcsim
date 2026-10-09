"""Rasterizes packaging/imcsim.svg into packaging/windows/imcsim.ico, without third-party packages.

The drawing is copied from the SVG (a rounded square, a zigzag and an S curve); rerun this script after
changing it. Every size is stored as PNG, which Windows reads in icons since Vista.
"""

import math
import pathlib
import struct
import zlib

Sizes = [16, 20, 24, 32, 40, 48, 64, 128, 256]
Background = (0x1E, 0x22, 0x29)
Zigzag = [(40, 92), (76, 92), (84, 76), (100, 108), (116, 76), (132, 108), (148, 76), (164, 108), (172, 92), (216, 92)]
ZigzagColor = (0xE1, 0xE6, 0xED)
CurveColor = (0x38, 0x84, 0xF4)


def cubic(p0, p1, p2, p3, steps=48):
    points = []
    for step in range(steps + 1):
        t = step / steps
        u = 1 - t
        points.append(tuple(u**3 * a + 3 * u * u * t * b + 3 * u * t * t * c + t**3 * d
                            for a, b, c, d in zip(p0, p1, p2, p3)))
    return points


# M48 176 C76 124 100 124 128 176 S180 228 208 176: the S reflects (100, 124) around (128, 176)
Curve = cubic((48, 176), (76, 124), (100, 124), (128, 176)) + cubic((128, 176), (156, 228), (180, 228), (208, 176))[1:]


def segment_distance(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def polyline_distance(px, py, points):
    return min(segment_distance(px, py, *points[i], *points[i + 1]) for i in range(len(points) - 1))


def rounded_rect_distance(px, py, x, y, size, radius):
    half = size / 2
    qx = abs(px - (x + half)) - (half - radius)
    qy = abs(py - (y + half)) - (half - radius)
    return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - radius


def over(dst, color, alpha):
    r, g, b, a = dst
    out_a = alpha + a * (1 - alpha)
    if out_a == 0:
        return (0, 0, 0, 0)
    mix = lambda src, base: (src * alpha + base * a * (1 - alpha)) / out_a
    return (mix(color[0], r), mix(color[1], g), mix(color[2], b), out_a)


def render(size):
    scale = size / 256
    rows = []
    for row in range(size):
        line = bytearray([0])
        for column in range(size):
            px, py = (column + 0.5) / scale, (row + 0.5) / scale
            pixel = (0, 0, 0, 0)
            # Distances become pixels, so each edge fades over one pixel at every size
            layers = [
                (Background, rounded_rect_distance(px, py, 8, 8, 240, 48)),
                (ZigzagColor, polyline_distance(px, py, Zigzag) - 5),
                (CurveColor, polyline_distance(px, py, Curve) - 6),
            ]
            for color, distance in layers:
                coverage = max(0.0, min(1.0, 0.5 - distance * scale))
                if coverage > 0:
                    pixel = over(pixel, color, coverage)
            line += bytes([round(pixel[0]), round(pixel[1]), round(pixel[2]), round(pixel[3] * 255)])
        rows.append(bytes(line))
    return png(size, b"".join(rows))


def png(size, raw):
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    header = struct.pack(">IIBBBBB", size, size, 8, 6, 0, 0, 0)
    return b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", header) + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b"")


def main():
    images = [render(size) for size in Sizes]
    offset = 6 + 16 * len(images)
    directory = struct.pack("<HHH", 0, 1, len(images))
    for size, image in zip(Sizes, images):
        directory += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(image), offset)
        offset += len(image)
    output = pathlib.Path(__file__).with_name("imcsim.ico")
    output.write_bytes(directory + b"".join(images))
    print(f"{output}: {len(Sizes)} sizes")


if __name__ == "__main__":
    main()
