import numpy as np
from PIL import Image, ImageDraw, ImageFont, ImageFilter
from scipy import ndimage as ndi

S = 2                          # render scale (retina)
LW, LH = 1000, 640             # logical canvas
W, H = LW * S, LH * S
rng = np.random.default_rng(11)

FONT_DIR = '/usr/share/fonts/opentype/inter/'
def font(name, size_logical):
    return ImageFont.truetype(FONT_DIR + name, int(round(size_logical * S)))

def f32(c):  # 0-255 tuple -> 0-1 float array
    return np.array(c, dtype=np.float32) / 255.0

def rr_mask(w, h, r, ss=3):
    """antialiased rounded-rect mask (h, w) in 0..1"""
    im = Image.new('L', (w * ss, h * ss), 0)
    ImageDraw.Draw(im).rounded_rectangle([0, 0, w * ss - 1, h * ss - 1], radius=r * ss, fill=255)
    im = im.resize((w, h), Image.LANCZOS)
    return np.asarray(im, dtype=np.float32) / 255.0

def paste_rgb(canvas, region_rgb, x, y, mask=None):
    h, w = region_rgb.shape[:2]
    sl = (slice(y, y + h), slice(x, x + w))
    if mask is None:
        canvas[sl] = region_rgb
    else:
        m = mask[..., None]
        canvas[sl] = canvas[sl] * (1 - m) + region_rgb * m

def blur(a, s):
    return ndi.gaussian_filter(a, s)

def shade(height, base, light=(-0.55, -0.7, 0.55), spec=0.35, shin=40.0, hscale=40.0, amb=0.30, diff=0.75):
    """height: (h,w) float; base: (h,w,3) colour. Returns lit colour."""
    gy, gx = np.gradient(height * hscale)
    n = np.stack([-gx, -gy, np.ones_like(gx)], axis=-1)
    n /= np.linalg.norm(n, axis=-1, keepdims=True)
    L = np.array(light, dtype=np.float32); L /= np.linalg.norm(L)
    d = np.clip((n * L).sum(-1), 0, 1)
    Hh = L + np.array([0, 0, 1], dtype=np.float32); Hh /= np.linalg.norm(Hh)
    s = np.clip((n * Hh).sum(-1), 0, 1) ** shin
    return np.clip(base * (amb + diff * d[..., None]) + spec * s[..., None], 0, 1)

def tracked_text(draw, xy, text, fnt, fill, tracking=0.0, anchor='l'):
    """draw text with letter spacing (tracking in px at render scale)."""
    total = sum(fnt.getlength(c) for c in text) + tracking * (len(text) - 1)
    x, y = xy
    if anchor == 'm': x -= total / 2
    elif anchor == 'r': x -= total
    for c in text:
        draw.text((x, y), c, font=fnt, fill=fill, anchor='ls')
        x += fnt.getlength(c) + tracking
    return total

def text_layer(w, h, items):
    """items: list of dict(xy,text,font,fill,tracking,anchor). returns (h,w) alpha, from white text"""
    im = Image.new('L', (w, h), 0)
    d = ImageDraw.Draw(im)
    for it in items:
        tracked_text(d, it['xy'], it['text'], it['font'], 255, it.get('tracking', 0), it.get('anchor', 'l'))
    return np.asarray(im, dtype=np.float32) / 255.0

def engrave(canvas, items, ink=(38, 36, 34), hi=1.0, shadow_off=1):
    """print text with a tiny engraved highlight underneath"""
    a = text_layer(canvas.shape[1], canvas.shape[0], items)
    a_hi = np.roll(a, shadow_off, axis=0)
    canvas[:] = canvas * (1 - a_hi[..., None] * 0.35) + a_hi[..., None] * 0.35 * hi
    ink = f32(ink)
    canvas[:] = canvas * (1 - a[..., None]) + ink * a[..., None]
