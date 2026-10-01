"""Render docs/data/depth_sweep.csv as docs/img/depth_sweep.svg.

No dependencies. The SVG carries its own light and dark themes
(prefers-color-scheme), so it reads correctly on GitHub in either mode.

    python3 docs/make_chart.py
"""

import csv
import math
from pathlib import Path

ROOT = Path(__file__).parent
rows = list(csv.DictReader(open(ROOT / "data" / "depth_sweep.csv")))
levels = [int(r["levels"]) for r in rows]
array = [float(r["array_mops"]) for r in rows]
mapl = [float(r["map_mops"]) for r in rows]

W, H = 760, 420
L, R, T, B = 64, 150, 78, 56  # plot margins; right margin holds direct labels
pw, ph = W - L - R, H - T - B
x_lo, x_hi = math.log10(30), math.log10(8000)
y_hi = 100.0


def x(v):
    return L + (math.log10(v) - x_lo) / (x_hi - x_lo) * pw


def y(v):
    return T + ph - v / y_hi * ph


def path(vals):
    return " ".join(f"{'M' if i == 0 else 'L'}{x(lv):.1f},{y(v):.1f}" for i, (lv, v) in enumerate(zip(levels, vals)))


out = []
add = out.append
add(f'<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 {W} {H}" width="{W}" height="{H}" '
    'font-family="-apple-system, BlinkMacSystemFont, Segoe UI, Helvetica, Arial, sans-serif" '
    'role="img" aria-labelledby="t d">')
add("""<style>
  .bg { fill: #fcfcfb } .ink { fill: #0b0b0b } .ink2 { fill: #52514e }
  .grid { stroke: #e6e5e0 } .axis { stroke: #b9b8b2 }
  .s1 { stroke: #2a78d6 } .f1 { fill: #2a78d6 } .s2 { stroke: #eb6834 } .f2 { fill: #eb6834 }
  .ring { stroke: #fcfcfb }
  @media (prefers-color-scheme: dark) {
    .bg { fill: #1a1a19 } .ink { fill: #ffffff } .ink2 { fill: #c3c2b7 }
    .grid { stroke: #2e2e2c } .axis { stroke: #55554f }
    .s1 { stroke: #3987e5 } .f1 { fill: #3987e5 } .s2 { stroke: #d95926 } .f2 { fill: #d95926 }
    .ring { stroke: #1a1a19 }
  }
</style>""")
add('<title id="t">Throughput vs number of price levels</title>')
add('<desc id="d">Flat array ladder stays near 90 million operations per second up to about 1,100 '
    'price levels, while std::map falls from 66 to 17 million. The array is 1.5 times faster at 40 '
    'levels and 3.8 times faster at about 6,200 levels.</desc>')
add(f'<rect class="bg" width="{W}" height="{H}" rx="8"/>')

# Title and subtitle
add(f'<text class="ink" x="{L - 40}" y="30" font-size="17" font-weight="600">Throughput vs book width</text>')
add(f'<text class="ink2" x="{L - 40}" y="52" font-size="13">Same 5M-order replay, ~10k resting orders, '
    'spread over more price levels. Median of 5 runs, M4 Pro.</text>')

# Grid and y axis
for v in range(0, 101, 20):
    add(f'<line class="grid" x1="{L}" x2="{L + pw}" y1="{y(v):.1f}" y2="{y(v):.1f}" stroke-width="1"/>')
    add(f'<text class="ink2" x="{L - 10}" y="{y(v) + 4:.1f}" font-size="12" text-anchor="end">{v}</text>')
add(f'<text class="ink2" x="{L - 44}" y="{T + ph / 2:.1f}" font-size="12" text-anchor="middle" '
    f'transform="rotate(-90 {L - 44} {T + ph / 2:.1f})">M ops/s</text>')

# x axis (log scale)
add(f'<line class="axis" x1="{L}" x2="{L + pw}" y1="{y(0):.1f}" y2="{y(0):.1f}" stroke-width="1"/>')
for v in (30, 100, 300, 1000, 3000):
    label = f"{v:,}"
    add(f'<line class="axis" x1="{x(v):.1f}" x2="{x(v):.1f}" y1="{y(0):.1f}" y2="{y(0) + 5:.1f}" stroke-width="1"/>')
    add(f'<text class="ink2" x="{x(v):.1f}" y="{y(0) + 20:.1f}" font-size="12" text-anchor="middle">{label}</text>')
add(f'<text class="ink2" x="{L + pw / 2:.1f}" y="{H - 12}" font-size="12" text-anchor="middle">'
    'Non-empty price levels (log scale)</text>')

# Series: 2px lines, 8px markers with a 2px surface ring, native tooltips
for vals, s, f, name in ((mapl, "s2", "f2", "std::map"), (array, "s1", "f1", "Flat array + bitmap")):
    add(f'<path class="{s}" d="{path(vals)}" fill="none" stroke-width="2" stroke-linejoin="round"/>')
    for lv, v, a, m in zip(levels, vals, array, mapl):
        add(f'<circle class="{f} ring" cx="{x(lv):.1f}" cy="{y(v):.1f}" r="4" stroke-width="2">'
            f'<title>{name}: {v:.1f}M ops/s at {lv:,} levels (array {a / m:.1f}x faster)</title></circle>')

# Direct labels at the line ends (text in ink; the mark beside it carries the colour)
for vals, f, name in ((array, "f1", "Flat array"), (mapl, "f2", "std::map")):
    ly = y(vals[-1])
    add(f'<rect class="{f}" x="{x(levels[-1]) + 12:.1f}" y="{ly - 5:.1f}" width="10" height="10" rx="2"/>')
    add(f'<text class="ink" x="{x(levels[-1]) + 28:.1f}" y="{ly + 4:.1f}" font-size="13">{name} '
        f'<tspan class="ink2">{vals[-1]:.0f}M</tspan></text>')

# Ratio callouts at both ends
for i in (0, len(levels) - 1):
    ratio = array[i] / mapl[i]
    mid = (y(array[i]) + y(mapl[i])) / 2
    add(f'<line class="axis" x1="{x(levels[i]):.1f}" x2="{x(levels[i]):.1f}" y1="{y(array[i]) + 7:.1f}" '
        f'y2="{y(mapl[i]) - 7:.1f}" stroke-width="1" stroke-dasharray="3 3"/>')
    anchor, dx = ("start", 8) if i == 0 else ("end", -8)
    add(f'<text class="ink" x="{x(levels[i]) + dx:.1f}" y="{mid + 4:.1f}" font-size="13" font-weight="600" '
        f'text-anchor="{anchor}">{ratio:.1f}x</text>')

# Legend (always present for 2 series)
lx, ly = L + pw - 250, T - 14
for i, (f, name) in enumerate((("f1", "Flat array + bitmap"), ("f2", "std::map"))):
    add(f'<rect class="{f}" x="{lx + i * 150}" y="{ly - 9}" width="10" height="10" rx="2"/>')
    add(f'<text class="ink2" x="{lx + i * 150 + 16}" y="{ly}" font-size="12">{name}</text>')

add("</svg>")
(ROOT / "img").mkdir(exist_ok=True)
(ROOT / "img" / "depth_sweep.svg").write_text("\n".join(out) + "\n")
print("wrote docs/img/depth_sweep.svg")
