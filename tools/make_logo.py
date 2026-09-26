"""Gera o logotipo do MOBILADOR (PNG + ICO) de forma determinística."""
from PIL import Image, ImageDraw, ImageFilter
import os
S = 1024
OUT = os.path.join(os.path.dirname(__file__), "..", "assets")

def grad(w, h, c1, c2):
    g = Image.new("RGB", (w, h))
    px = g.load()
    for y in range(h):
        for x in range(w):
            t = (x + y) / (w + h)
            px[x, y] = tuple(int(c1[i] + (c2[i] - c1[i]) * t) for i in range(3))
    return g

def logo():
    img = Image.new("RGBA", (S, S), (0, 0, 0, 0))
    # base: squircle escuro
    base = Image.new("L", (S, S), 0)
    ImageDraw.Draw(base).rounded_rectangle((32, 32, S - 32, S - 32), radius=230, fill=255)
    bg = grad(S, S, (20, 26, 40), (8, 10, 18))
    img.paste(bg, (0, 0), base)
    # "M" como traço contínuo (celular -> PC): polilinha grossa com gradiente
    mask = Image.new("L", (S, S), 0)
    d = ImageDraw.Draw(mask)
    pts = [(250, 740), (250, 300), (512, 560), (774, 300), (774, 740)]
    d.line(pts, fill=255, width=110, joint="curve")
    for p in (pts[0], pts[-1]):
        d.ellipse((p[0] - 55, p[1] - 55, p[0] + 55, p[1] + 55), fill=255)
    fg = grad(S, S, (0, 229, 255), (124, 77, 255))
    glow = mask.filter(ImageFilter.GaussianBlur(40))
    glow_layer = Image.new("RGBA", (S, S), (0, 200, 255, 0))
    glow_layer.putalpha(glow.point(lambda v: int(v * 0.55)))
    img = Image.alpha_composite(img, glow_layer)
    img.paste(fg, (0, 0), mask)
    # traços de velocidade
    sp = ImageDraw.Draw(img)
    for i, (x0, y) in enumerate([(120, 820), (170, 870)]):
        sp.rounded_rectangle((x0, y - 12, x0 + 300 - i * 90, y + 12), radius=12, fill=(0, 229, 255, 200 - i * 70))
    sp.ellipse((820, 820, 880, 880), fill=(124, 77, 255, 255))
    return img

os.makedirs(OUT, exist_ok=True)
L = logo()
L.resize((512, 512), Image.LANCZOS).save(os.path.join(OUT, "mobilador.png"))
L.resize((256, 256), Image.LANCZOS).save(os.path.join(OUT, "mobilador.ico"),
    sizes=[(16, 16), (24, 24), (32, 32), (48, 48), (64, 64), (128, 128), (256, 256)])
L.resize((64, 64), Image.LANCZOS).save(os.path.join(OUT, "mobilador_64.png"))
print("ok")
