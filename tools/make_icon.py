#!/usr/bin/env python3
"""Generates the Beat It app icon.

    python3 tools/make_icon.py

writes resources/icon.svg and, if rsvg-convert + Pillow are available,
resources/icon_1024.png (dithered to avoid gradient banding)."""
import math, os, random, shutil, subprocess

HERE = os.path.dirname(os.path.abspath(__file__))
RES = os.path.join(HERE, '..', 'resources')
W = 1024
X0, Y0, S, R = 100, 100, 824, 186          # macOS icon grid: 824 px rounded square

# five beats, spacing shrinking left -> right (tempo speeding up)
beats, x, gap = [], 222, 190
for _ in range(5):
    beats.append(x)
    x += gap
    gap *= 0.80

mid = 520
def amp(px):
    a = 0.16
    for b in beats:
        if px >= b - 6:
            a = max(a, 0.97 * math.exp(-(px - b) / 70.0))
    return a

bars, x, i = [], X0 + 86, 0
bw, step = 30, 46
while x + bw < X0 + S - 70:
    h = max(20, amp(x + bw / 2) * (0.86 + 0.14 * math.sin(i * 2.1)) * 230)
    bars.append(f'<rect x="{x:.1f}" y="{mid - h:.1f}" width="{bw}" height="{2*h:.1f}" rx="{bw/2}" fill="url(#wave)"/>')
    x += step
    i += 1

lines = []
for k, b in enumerate(beats):
    w = 22 if k == 0 else 16
    op = 1.0 if k == 0 else 0.9
    # dark keyline so the beat lines read on top of the waveform
    lines.append(f'<rect x="{b - w/2 - 5:.1f}" y="262" width="{w + 10}" height="540" rx="{w/2+5}" fill="#14161c" opacity="0.85"/>')
    lines.append(f'<rect x="{b - w/2:.1f}" y="266" width="{w}" height="532" rx="{w/2}" fill="#ff8a3d" opacity="{op}"/>')
    lines.append(f'<path d="M {b-30:.1f} 196 L {b+30:.1f} 196 L {b:.1f} 240 Z" fill="#ff8a3d" opacity="{op}" stroke="#ff8a3d" stroke-width="8" stroke-linejoin="round"/>')

pts = []
for t in range(0, 101):
    px = X0 + 92 + (S - 184) * t / 100
    py = 772 - 170 / (1 + math.exp(-(t - 52) / 12))
    pts.append(f'{px:.1f},{py:.1f}')
curve = ' '.join(pts)

svg = f'''<svg xmlns="http://www.w3.org/2000/svg" width="{W}" height="{W}" viewBox="0 0 {W} {W}">
  <defs>
    <linearGradient id="bg" x1="0" y1="0" x2="0.35" y2="1">
      <stop offset="0" stop-color="#303644"/>
      <stop offset="1" stop-color="#0f1116"/>
    </linearGradient>
    <linearGradient id="wave" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#9ccdff"/>
      <stop offset="0.5" stop-color="#5f95dc"/>
      <stop offset="1" stop-color="#3b66ad"/>
    </linearGradient>
    <radialGradient id="glowbg" cx="0.5" cy="0.35" r="0.7">
      <stop offset="0" stop-color="#ff8a3d" stop-opacity="0.10"/>
      <stop offset="1" stop-color="#ff8a3d" stop-opacity="0"/>
    </radialGradient>
    <linearGradient id="sheen" x1="0" y1="0" x2="0" y2="1">
      <stop offset="0" stop-color="#ffffff" stop-opacity="0.09"/>
      <stop offset="0.4" stop-color="#ffffff" stop-opacity="0"/>
    </linearGradient>
    <filter id="shadow" x="-20%" y="-20%" width="140%" height="140%">
      <feDropShadow dx="0" dy="14" stdDeviation="18" flood-color="#000" flood-opacity="0.45"/>
    </filter>
    <filter id="glow" x="-10%" y="-40%" width="120%" height="180%">
      <feGaussianBlur stdDeviation="10"/>
    </filter>
    <clipPath id="clip"><rect x="{X0}" y="{Y0}" width="{S}" height="{S}" rx="{R}"/></clipPath>
  </defs>
  <rect x="{X0}" y="{Y0}" width="{S}" height="{S}" rx="{R}" fill="url(#bg)" filter="url(#shadow)"/>
  <g clip-path="url(#clip)">
    <rect x="{X0}" y="{Y0}" width="{S}" height="{S}" fill="url(#glowbg)"/>
    {''.join(bars)}
    {''.join(lines)}
    <polyline points="{curve}" fill="none" stroke="#4cc2ff" stroke-width="34" stroke-linecap="round" opacity="0.5" filter="url(#glow)"/>
    <polyline points="{curve}" fill="none" stroke="#0f1116" stroke-width="30" stroke-linecap="round" stroke-linejoin="round" opacity="0.6"/>
    <polyline points="{curve}" fill="none" stroke="#4cc2ff" stroke-width="20" stroke-linecap="round" stroke-linejoin="round"/>
    <rect x="{X0}" y="{Y0}" width="{S}" height="{S}" fill="url(#sheen)"/>
  </g>
  <rect x="{X0+1.5}" y="{Y0+1.5}" width="{S-3}" height="{S-3}" rx="{R-1.5}" fill="none" stroke="#ffffff" stroke-opacity="0.10" stroke-width="3"/>
</svg>
'''
os.makedirs(RES, exist_ok=True)
svg_path = os.path.join(RES, 'icon.svg')
open(svg_path, 'w').write(svg)
print('wrote', os.path.normpath(svg_path))

if shutil.which('rsvg-convert'):
    png_path = os.path.join(RES, 'icon_1024.png')
    subprocess.run(['rsvg-convert', '-w', '1024', '-h', '1024', svg_path, '-o', png_path], check=True)
    try:
        from PIL import Image
        im = Image.open(png_path).convert('RGBA')
        px = im.load()
        rnd = random.Random(1)
        for y in range(im.height):            # +-1 LSB dither against banding
            for x in range(im.width):
                r, g, b, a = px[x, y]
                if a:
                    n = rnd.choice((-1, 0, 0, 1))
                    px[x, y] = (min(255, max(0, r + n)), min(255, max(0, g + n)), min(255, max(0, b + n)), a)
        im.save(png_path, optimize=True)
    except ImportError:
        pass
    print('wrote', os.path.normpath(png_path))

# ---- full macOS iconset -> resources/AppIcon.icns (all sizes 16..1024)
if shutil.which('iconutil'):
    try:
        from PIL import Image
        import tempfile
        src = Image.open(os.path.join(RES, 'icon_1024.png')).convert('RGBA')
        with tempfile.TemporaryDirectory() as tmp:
            iconset = os.path.join(tmp, 'AppIcon.iconset')
            os.makedirs(iconset)
            for base in (16, 32, 128, 256, 512):
                for scale in (1, 2):
                    px = base * scale
                    name = f'icon_{base}x{base}' + ('@2x' if scale == 2 else '') + '.png'
                    src.resize((px, px), Image.LANCZOS).save(os.path.join(iconset, name))
            icns = os.path.join(RES, 'AppIcon.icns')
            subprocess.run(['iconutil', '-c', 'icns', iconset, '-o', icns], check=True)
            print('wrote', os.path.normpath(icns))
    except ImportError:
        print('Pillow not installed: skipping AppIcon.icns')
