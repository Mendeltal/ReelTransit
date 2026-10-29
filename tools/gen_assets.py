from gen_common import *
import os, math

OUT = '../Assets'; os.makedirs(OUT, exist_ok=True)
SRC = '../Source'

# ---------------- layout (logical px) ----------------
CHEEK = 52
VU_IN  = (72, 96, 200, 150)
VU_OUT = (728, 96, 200, 150)
REEL_WIN = (292, 84, 416, 176)
REEL_L = (390, 172); REEL_R = (610, 172); REEL_R_PX = 74
KEY_W, KEY_H = 100, 82
KEY_Y = 292
KEY_X0, KEY_DX = 96, 118
KEYS = [('stop','STOP','tape stop'), ('reverse','REVERSE','reel back'), ('stutter','STUTTER','roll / loop'),
        ('rise','RISE','build up'), ('throw','THROW','echo throw'), ('spinUp','SPIN UP','on release'),
        ('latch','LATCH','hold on')]
KNOB_Y = 484; KNOB_D = 56; KNOB_X0 = 98; KNOB_DX = 71.3
KNOBS = [('stopTime','TIME',0,0), ('stopCurve','CURVE',0,0),
         ('length','LENGTH',6,1), ('roll','ROLL',0,1), ('pitch','PITCH',0,1),
         ('riseTime','TIME',0,2), ('riseRange','RANGE',0,2),
         ('throwDiv','DIV',6,3), ('throwFb','FEEDBACK',0,3),
         ('age','AGE',0,4), ('drive','DRIVE',0,4), ('output','OUTPUT',0,4)]
GROUPS = ['STOP', 'STUTTER', 'RISE', 'ECHO THROW', 'TAPE']
PLATE = (684, 562, 244, 50)
VU_PIVOT = (100, 204); VU_R = 142; VU_ANG = 32.0; VU_MIN = -20.0; VU_MAX = 3.0

# ---------------- helpers ----------------
def recess(cv, x, y, w, h, r, fill=None, depth=7, strength=0.65):
    x, y, w, h = [int(round(v * S)) for v in (x, y, w, h)]; r = int(r * S)
    m = rr_mask(w, h, r)
    pad = int(depth * S * 3)
    big = np.zeros((h + 2 * pad, w + 2 * pad), np.float32); big[pad:pad + h, pad:pad + w] = m
    outside = 1 - big
    sh = blur(outside, depth * S * 0.55)
    sh = np.roll(sh, int(depth * 0.5 * S), axis=0)[pad:pad + h, pad:pad + w]   # shadow falls from top edge
    region = cv[y:y + h, x:x + w]
    if fill is not None:
        region[:] = region * (1 - m[..., None]) + f32(fill) * m[..., None]
    region[:] = region * (1 - (sh * m * strength)[..., None])
    # bottom-lip highlight outside
    lip = np.clip(blur(m, 1.2 * S) - m, 0, 1)
    lipy = np.roll(lip, int(1.2 * S), axis=0)
    ys0, ys1 = max(y - 4 * S, 0), y + h + 4 * S
    # draw light line below the recess
    hl = np.zeros((h + 8 * S, w), np.float32)
    mm = np.zeros((h + 8 * S, w), np.float32); mm[4 * S:4 * S + h] = m
    low = np.clip(np.roll(mm, int(1.5 * S), axis=0) - mm, 0, 1)
    sl = cv[y - 4 * S:y + h + 4 * S, x:x + w]
    sl[:] = sl * (1 - low[..., None] * 0.5) + low[..., None] * 0.5 * 1.0

def text_items(cv, items, ink=(40, 38, 35), hi=1.0):
    engrave(cv, items, ink=ink, hi=hi, shadow_off=max(1, S // 2))

def P(v): return v * S

# ---------------- faceplate ----------------
def make_faceplate():
    cv = np.zeros((H, W, 3), np.float32)
    # brushed champagne aluminium
    n = rng.standard_normal((H, W)).astype(np.float32)
    streak = ndi.gaussian_filter(n, (0.6, 55 * S / 2))
    streak /= streak.std() + 1e-6
    fine = ndi.gaussian_filter(rng.standard_normal((H, W)).astype(np.float32), (0.5, 6)); fine /= fine.std()
    hmap = (streak * 0.5 + fine * 0.25) * 0.010
    base = np.ones((H, W, 3), np.float32) * f32((198, 190, 174))
    # large soft light falloff
    yy, xx = np.mgrid[0:H, 0:W].astype(np.float32)
    g = 1.05 - 0.22 * ((xx / W - 0.35) ** 2 + (yy / H - 0.2) ** 2) * 2.0
    base *= g[..., None]
    lit = shade(hmap, base, hscale=40, spec=0.05, amb=0.78, diff=0.30)
    lit += (streak[..., None] * 0.020)
    cv[:] = np.clip(lit, 0, 1)
    # walnut cheeks
    cw = CHEEK * S
    for (x0, flip) in ((0, False), (W - cw, True)):
        wn = ndi.gaussian_filter(rng.standard_normal((H, cw)).astype(np.float32), (90 * S / 2, 1.2))
        wn /= wn.std()
        yy2, xx2 = np.mgrid[0:H, 0:cw].astype(np.float32)
        rings = np.sin((xx2 * 0.045 + wn * 1.8 + yy2 * 0.0006) * (1 if not flip else 1.2))
        pore = ndi.gaussian_filter(rng.standard_normal((H, cw)).astype(np.float32), (14, 0.5)); pore /= pore.std()
        t = np.clip(0.5 + 0.22 * rings + 0.10 * wn + 0.06 * pore, 0, 1)
        dark = f32((46, 26, 15)); light = f32((124, 74, 40))
        col = dark + (light - dark) * t[..., None]
        wh = (wn * 0.002 + pore * 0.0008)
        col = shade(wh, col, hscale=30, spec=0.10, shin=30, amb=0.55, diff=0.55)
        # curvature: darker toward panel edge, subtle roll-off
        edge = (xx2 / cw) if not flip else (1 - xx2 / cw)
        col *= (0.78 + 0.30 * edge)[..., None] if False else (0.80 + 0.25 * (1 - np.abs(edge - 0.45)))[..., None]
        cv[:, x0:x0 + cw] = np.clip(col, 0, 1)
    # inner bevel between cheek and panel
    for x in (cw, W - cw):
        for k in range(4 * S):
            a = 0.30 * (1 - k / (4 * S))
            xx_ = x + k if x == cw else x - k - 1
            cv[:, xx_] = cv[:, xx_] * (1 - a) + a * 0.15
    # recess windows
    recess(cv, *VU_IN, 10, fill=(18, 18, 19), depth=8)
    recess(cv, *VU_OUT, 10, fill=(18, 18, 19), depth=8)
    recess(cv, *REEL_WIN, 14, fill=(12, 12, 13), depth=10, strength=0.8)
    # key wells
    for i in range(len(KEYS)):
        x = KEY_X0 + i * KEY_DX
        recess(cv, x - 5, KEY_Y - 5, KEY_W + 10, KEY_H + 10, 12, fill=(24, 24, 25), depth=6)
    # engraved text
    items = []
    f_title = font('InterDisplay-Bold.otf', 22); f_sub = font('Inter-Medium.otf', 8.5)
    f_small = font('Inter-SemiBold.otf', 8.2); f_grp = font('Inter-Bold.otf', 8.6)
    items.append(dict(xy=(P(CHEEK + 28), P(52)), text='REEL TRANSIT', font=f_title, tracking=P(5.5)))
    items.append(dict(xy=(P(1000 - CHEEK - 28), P(52)), text='STEREO TAPE TRANSIT PROCESSOR', font=f_sub, tracking=P(2.6), anchor='r'))
    items.append(dict(xy=(P(CHEEK + 28), P(66)), text='MODEL RT-1  ·  FULL-TRACK TRANSITION ENGINE', font=f_sub, tracking=P(2.2)))
    # VU labels
    items.append(dict(xy=(P(VU_IN[0] + 100), P(VU_IN[1] + VU_IN[3] + 18)), text='INPUT', font=f_small, tracking=P(3), anchor='m'))
    items.append(dict(xy=(P(VU_OUT[0] + 100), P(VU_OUT[1] + VU_OUT[3] + 18)), text='OUTPUT', font=f_small, tracking=P(3), anchor='m'))
    # reel window label
    items.append(dict(xy=(P(500), P(REEL_WIN[1] + REEL_WIN[3] + 18)), text='SUPPLY  ·  TAKE-UP', font=f_small, tracking=P(3), anchor='m'))
    # knob labels + group headers
    f_lab = font('Inter-SemiBold.otf', 8.2)
    for i, (pid, lab, steps, grp) in enumerate(KNOBS):
        cx = KNOB_X0 + i * KNOB_DX
        items.append(dict(xy=(P(cx), P(KNOB_Y + 50)), text=lab, font=f_lab, tracking=P(1.6), anchor='m'))
    for gi, gname in enumerate(GROUPS):
        idx = [i for i, k in enumerate(KNOBS) if k[3] == gi]
        xa = KNOB_X0 + idx[0] * KNOB_DX - 32; xb = KNOB_X0 + idx[-1] * KNOB_DX + 32
        mid = (xa + xb) / 2
        items.append(dict(xy=(P(mid), P(KNOB_Y - 56)), text=gname, font=f_grp, tracking=P(3.4), anchor='m'))
    text_items(cv, items)
    # group header rules (engraved lines left/right of the text)
    im = Image.fromarray((cv * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for gi, gname in enumerate(GROUPS):
        idx = [i for i, k in enumerate(KNOBS) if k[3] == gi]
        xa = KNOB_X0 + idx[0] * KNOB_DX - 32; xb = KNOB_X0 + idx[-1] * KNOB_DX + 32
        tw = f_grp.getlength(gname) / S + 3.4 * (len(gname) - 1) + 8
        mid = (xa + xb) / 2; y = KNOB_Y - 59
        for (a, b) in ((xa, mid - tw / 2), (mid + tw / 2, xb)):
            if b - a > 4:
                d.line([P(a), P(y), P(b), P(y)], fill=(70, 66, 60), width=max(1, S // 2))
                d.line([P(a), P(y) + 1, P(b), P(y) + 1], fill=(236, 230, 214), width=max(1, S // 2))
        # bracket ends
    cv = np.asarray(im, dtype=np.float32) / 255.0
    # knob scale ticks
    im = Image.fromarray((cv * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for i, (pid, lab, steps, grp) in enumerate(KNOBS):
        cx = KNOB_X0 + i * KNOB_DX
        n = steps if steps else 11
        for t in range(n):
            a = math.radians(-135 + 270 * t / (n - 1))
            major = (t % 5 == 0) if not steps else True
            r0 = KNOB_D / 2 + 5; r1 = r0 + (5 if major else 3)
            p0 = (P(cx + r0 * math.sin(a)), P(KNOB_Y - r0 * math.cos(a)))
            p1 = (P(cx + r1 * math.sin(a)), P(KNOB_Y - r1 * math.cos(a)))
            d.line([p0, p1], fill=(52, 49, 45), width=max(1, int(S * (1.3 if major else 0.9))))
    cv = np.asarray(im, dtype=np.float32) / 255.0
    # brass signature plate
    px, py, pw, ph = [int(v * S) for v in PLATE]
    br = ndi.gaussian_filter(rng.standard_normal((ph, pw)).astype(np.float32), (0.5, 30 * S / 2)); br /= br.std()
    m = rr_mask(pw, ph, int(5 * S))
    yy3 = np.linspace(0, 1, ph)[:, None]
    base = np.ones((ph, pw, 3), np.float32) * f32((176, 136, 66)) * (1.12 - 0.35 * yy3)[..., None]
    base = base + br[..., None] * 0.02
    lit = shade(br * 0.004, base, hscale=40, spec=0.15, amb=0.8, diff=0.3)
    # bevel
    dist = ndi.distance_transform_edt(m > 0.5)
    bev = np.clip(1 - dist / (2.2 * S), 0, 1)
    lit = lit * (1 - 0.0) + (bev[..., None] * (0.30 * np.linspace(1, -1, ph)[:, None, None]))
    region = cv[py:py + ph, px:px + pw]
    region[:] = region * (1 - m[..., None]) + np.clip(lit, 0, 1) * m[..., None]
    # drop shadow below plate
    shm = np.roll(blur(np.pad(m, 8 * S), 2.5 * S), 2 * S, axis=0)[8 * S:8 * S + ph, 8 * S:8 * S + pw]
    # screws
    im = Image.fromarray((cv * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for sx, sy in ((px + 9 * S, py + ph // 2), (px + pw - 9 * S, py + ph // 2)):
        rr = 3.4 * S
        d.ellipse([sx - rr, sy - rr, sx + rr, sy + rr], fill=(120, 94, 48), outline=(60, 46, 22))
        d.line([sx - rr * 0.7, sy - rr * 0.4, sx + rr * 0.7, sy + rr * 0.4], fill=(52, 40, 18), width=max(1, S))
    # inner engraved border line on plate
    d.rounded_rectangle([px + 18 * S, py + 5 * S, px + pw - 18 * S, py + ph - 5 * S], radius=3 * S, outline=(112, 84, 38), width=max(1, S // 2))
    cv = np.asarray(im, dtype=np.float32) / 255.0
    # tiny corner screws on the panel
    im = Image.fromarray((cv * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    for sx, sy in ((CHEEK + 14, 14), (1000 - CHEEK - 14, 14), (CHEEK + 14, 626), (1000 - CHEEK - 14, 626)):
        rr = 4.2 * S; cx_, cy_ = sx * S, sy * S
        d.ellipse([cx_ - rr, cy_ - rr, cx_ + rr, cy_ + rr], fill=(150, 144, 132), outline=(70, 66, 60))
        d.line([cx_ - rr * .75, cy_ - rr * .25, cx_ + rr * .75, cy_ + rr * .25], fill=(60, 56, 50), width=max(1, S))
    cv = np.asarray(im, dtype=np.float32) / 255.0
    Image.fromarray((np.clip(cv, 0, 1) * 255).astype(np.uint8)).save(f'{OUT}/faceplate.png')

# ---------------- key caps ----------------
def make_cap(key, title, sub, down):
    w, h = KEY_W * S, KEY_H * S
    m = rr_mask(w, h, int(9 * S))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # dome / pillow height
    u = (xx / w - 0.5) * 2; v = (yy / h - 0.5) * 2
    pillow = (1 - np.clip(np.abs(u), 0, 1) ** 6) * (1 - np.clip(np.abs(v), 0, 1) ** 6)
    dist = ndi.distance_transform_edt(m > 0.5)
    roll = np.clip(dist / (4.5 * S), 0, 1) ** 0.6
    hm = (roll * 0.55 + pillow * 0.12) * (0.65 if down else 1.0)
    fine = ndi.gaussian_filter(rng.standard_normal((h, w)).astype(np.float32), 0.8) * 0.0006
    base = np.ones((h, w, 3), np.float32) * (f32((38, 38, 40)) if not down else f32((26, 26, 28)))
    lit = shade(hm + fine, base, hscale=60, spec=0.22 if not down else 0.10, shin=25, amb=0.55, diff=0.65)
    cap = lit
    # legend
    f_t = font('InterDisplay-Bold.otf', 13.5); f_s = font('Inter-Medium.otf', 7.6)
    im = Image.new('L', (w, h), 0); d = ImageDraw.Draw(im)
    tracked_text(d, (w / 2, h * 0.60), title, f_t, 255, tracking=P(1.5), anchor='m')
    tracked_text(d, (w / 2, h * 0.80), sub.upper(), f_s, 255, tracking=P(1.8), anchor='m')
    a = np.asarray(im, dtype=np.float32) / 255.0
    ink = f32((236, 226, 200)) if not down else f32((255, 214, 130))
    cap = cap * (1 - a[..., None] * 0.95) + ink * a[..., None] * 0.95
    if down:
        # lamp glow wash
        glow = blur(a, 5 * S)[..., None] * f32((255, 170, 60)) * 0.55
        cap = np.clip(cap + glow, 0, 1)
    # lamp socket (JUCE draws the lamp light itself)
    im2 = Image.fromarray((np.clip(cap, 0, 1) * 255).astype(np.uint8)); d2 = ImageDraw.Draw(im2)
    cx, cy, rr = w / 2, 13 * S, 3.6 * S
    d2.ellipse([cx - rr - S, cy - rr - S, cx + rr + S, cy + rr + S], fill=(14, 14, 15), outline=(70, 70, 72))
    cap = np.asarray(im2, dtype=np.float32) / 255.0
    rgba = np.dstack([cap, m])
    Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA').save(f'{OUT}/cap_{key}_{"down" if down else "up"}.png')

# ---------------- knob ----------------
def make_knob():
    D = KNOB_D; PAD = 8
    size = (D + 2 * PAD) * S
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2; r = np.hypot(xx - c, yy - c); ang = np.arctan2(yy - c, xx - c)
    R = D / 2 * S
    # antialiased disc mask
    mk = np.clip(R - r + 0.5, 0, 1)
    rn = r / R
    h = np.zeros_like(rn)
    # knurled outer skirt
    skirt = (rn > 0.78) & (rn <= 1.0)
    h += np.where(skirt, 0.10 + 0.035 * np.sin(ang * 60), 0)
    # chamfer between skirt and top
    h += np.where((rn > 0.70) & (rn <= 0.78), (0.78 - rn) / 0.08 * -0.06 + 0.12, 0)
    # top face: concentric machining + gentle dish
    top = rn <= 0.70
    h += np.where(top, 0.045 + 0.004 * np.sin(rn * 140) - 0.04 * rn ** 2, 0)
    # brushed spin highlight
    base = np.ones_like(h)[..., None] * f32((34, 34, 36))
    lit = shade(h, base, hscale=36, spec=0.5, shin=20, amb=0.5, diff=0.8)
    # anisotropic conic highlight on top
    cone = (np.cos((ang - 0.9) * 2) ** 6) * 0.20 + (np.cos((ang + 2.1) * 2) ** 6) * 0.12
    lit = np.clip(lit + (cone * top)[..., None] * f32((255, 245, 225)) * 0.55, 0, 1)
    # edge darkening
    lit *= (1 - 0.25 * np.clip((rn - 0.96) / 0.04, 0, 1))[..., None]
    # shadow
    sh = ndi.shift(blur(mk, 3.5 * S), (2.8 * S, 1.2 * S))
    a = mk + sh * 0.55 * (1 - mk)
    rgb = lit * mk[..., None]
    rgba = np.dstack([rgb, a])
    Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA').save(f'{OUT}/knob.png')

# ---------------- reel ----------------
def make_reel():
    Rl = REEL_R_PX; size = 2 * Rl * S
    yy, xx = np.mgrid[0:size, 0:size].astype(np.float32)
    c = size / 2; r = np.hypot(xx - c, yy - c); ang = np.arctan2(yy - c, xx - c)
    R = Rl * S; rn = r / R
    disc = np.clip(R - r + 0.5, 0, 1)
    # three big cut-out windows between spokes
    sect = (np.cos(3 * ang) > 0.0)       # 3 lobes
    spoke_w = 0.20
    angdist = np.abs(((ang * 3 / (2 * math.pi)) % 1) - 0.5) * 2   # 0 at spoke centre? tune
    ang3 = np.cos(3 * ang)
    window = (rn > 0.34) & (rn < 0.86) & (ang3 > -0.15 + 0.0) & ~(np.abs(ang3 - 1) < 0) # placeholder
    # cut windows centred at 0°, 120°, 240° (+60° offset), width via cos threshold
    win = (np.cos(3 * (ang - math.pi / 3)) > 0.35) & (rn > 0.40) & (rn < 0.84)
    winm = ndi.gaussian_filter(win.astype(np.float32), 0.7 * S)
    alpha = disc * (1 - winm)
    h = np.zeros_like(rn)
    h += np.where(rn <= 1.0, 0.06, 0) + np.where((rn > 0.90) & (rn <= 1.0), 0.05 * np.sin((rn - 0.90) / 0.10 * math.pi), 0)
    h += np.where(rn < 0.36, 0.05 + 0.04 * np.sin(rn * 60), 0)
    h += 0.003 * np.sin(rn * 110)
    base = np.ones_like(h)[..., None] * f32((206, 204, 198))
    lit = shade(h, base, hscale=50, spec=0.45, shin=24, amb=0.55, diff=0.55)
    cone = (np.cos((ang - 0.6) * 2) ** 8) * 0.28
    lit = np.clip(lit + cone[..., None] * 0.6, 0, 1)
    # window edge bevel (dark inner wall)
    edge = np.clip(winm - ndi.shift(winm, (1.5 * S, 1.5 * S)), 0, 1)
    lit = lit * (1 - edge[..., None] * 0.4)
    # centre nut + spindle holes
    nut = np.clip(0.17 * R - r + 0.5, 0, 1)
    nh = (0.04 + 0.02 * np.cos(6 * ang) * (r < 0.17 * R)) * nut
    nl = shade(nh + 0.0, np.ones_like(h)[..., None] * f32((120, 118, 114)), hscale=60, spec=0.8, shin=18, amb=0.5, diff=0.7)
    lit = lit * (1 - nut[..., None]) + nl * nut[..., None]
    hole = np.clip(0.06 * R - r + 0.5, 0, 1)
    lit = lit * (1 - hole[..., None]) + f32((10, 10, 10)) * hole[..., None]
    # tiny index mark on flange so the rotation reads
    mark = np.clip(0.035 * R - np.hypot(xx - c - 0.92 * R * math.cos(0.2), yy - c - 0.92 * R * math.sin(0.2)) + 0.5, 0, 1)
    lit = lit * (1 - mark[..., None]) + f32((200, 40, 30)) * mark[..., None]
    rgba = np.dstack([lit * alpha[..., None], alpha])
    Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA').save(f'{OUT}/reel.png')

# ---------------- VU meters ----------------
def make_vu(name, label):
    w, h = VU_IN[2] * S, VU_IN[3] * S
    m = rr_mask(w, h, int(8 * S))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # warm cream face with soft lamp falloff from the top
    light = 1.0 - 0.22 * (yy / h) ** 1.6 - 0.10 * (np.abs(xx / w - 0.5) * 2) ** 2
    face = np.ones((h, w, 3), np.float32) * f32((238, 226, 190)) * light[..., None]
    face += ndi.gaussian_filter(rng.standard_normal((h, w)).astype(np.float32), 1.0)[..., None] * 0.004
    im = Image.fromarray((np.clip(face, 0, 1) * 255).astype(np.uint8)); d = ImageDraw.Draw(im)
    px, py = VU_PIVOT[0] * S, VU_PIVOT[1] * S
    def ang_of(db): return math.radians((db - (VU_MIN + VU_MAX) / 2) / (VU_MAX - VU_MIN) * 2 * VU_ANG)
    def pt(db, r): a = ang_of(db); return (px + r * S * math.sin(a), py - r * S * math.cos(a))
    ink = (48, 42, 36); red = (178, 38, 30)
    # arc
    steps = 200
    for seg in range(steps):
        d0 = VU_MIN + (VU_MAX - VU_MIN) * seg / steps; d1 = VU_MIN + (VU_MAX - VU_MIN) * (seg + 1) / steps
        col = red if d0 >= 0 else ink
        d.line([pt(d0, VU_R), pt(d1, VU_R)], fill=col, width=max(2, int(1.4 * S)))
    # red zone band
    for seg in range(60):
        d0 = 0 + 3.0 * seg / 60; d1 = 0 + 3.0 * (seg + 1) / 60
        d.line([pt(d0, VU_R - 6), pt(d1, VU_R - 6)], fill=red, width=int(3.0 * S))
    f_n = font('Inter-SemiBold.otf', 8.6); f_big = font('InterDisplay-Bold.otf', 13)
    for db in [-20, -10, -7, -5, -3, -2, -1, 0, 1, 2, 3]:
        major = db in (-20, -10, -7, -5, -3, 0, 3)
        col = red if db >= 0 else ink
        d.line([pt(db, VU_R), pt(db, VU_R + (9 if major else 5))], fill=col, width=max(1, int(S * (1.5 if major else 1.0))))
        if major:
            tx, ty = pt(db, VU_R + 17)
            lab = ('+' if db > 0 else '') + str(db) if db != 0 else '0'
            lab = str(abs(db)) if db <= 0 else '+' + str(db)
            d.text((tx, ty), lab, font=f_n, fill=col, anchor='mm')
    # minor ticks every 1 dB between -20 and -10 look busy - skip
    d.text((w / 2, 104 * S), 'VU', font=f_big, fill=ink, anchor='mm')
    d.text((w / 2, 130 * S), label, font=font('Inter-Medium.otf', 7.5), fill=(110, 98, 80), anchor='mm')
    # pivot cover cap hint at bottom (needle drawn by plugin)
    face = np.asarray(im, dtype=np.float32) / 255.0
    # inner vignette
    dist = ndi.distance_transform_edt(m > 0.5)
    vig = np.clip(dist / (10 * S), 0, 1) ** 0.5
    face *= (0.80 + 0.20 * vig)[..., None]
    rgba = np.dstack([face, m])
    Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA').save(f'{OUT}/{name}.png')

def make_glass():
    w, h = VU_IN[2] * S, VU_IN[3] * S
    m = rr_mask(w, h, int(8 * S))
    yy, xx = np.mgrid[0:h, 0:w].astype(np.float32)
    # diagonal soft reflection
    t = (xx / w * 0.9 + yy / h * 0.5)
    refl = np.clip(1 - np.abs(t - 0.42) / 0.30, 0, 1) ** 2 * 0.12
    refl += np.clip(1 - np.abs(t - 0.78) / 0.05, 0, 1) * 0.05
    top = np.clip(1 - yy / (h * 0.45), 0, 1) ** 2 * 0.08
    a = (refl + top) * m
    # edge shadow
    dist = ndi.distance_transform_edt(m > 0.5)
    sh = (1 - np.clip(dist / (7 * S), 0, 1)) ** 1.5 * 0.45 * m
    rgb = np.ones((h, w, 3), np.float32)
    al = np.clip(a + sh, 0, 1)
    rgb = rgb * (a / np.maximum(al, 1e-4))[..., None]
    rgba = np.dstack([rgb, al])
    Image.fromarray((np.clip(rgba, 0, 1) * 255).astype(np.uint8), 'RGBA').save(f'{OUT}/vu_glass.png')

make_faceplate()
for (k, t, s_) in KEYS:
    make_cap(k, t, s_, False); make_cap(k, t, s_, True)
make_knob(); make_reel()
make_vu('vu_in', 'INPUT'); make_vu('vu_out', 'OUTPUT'); make_glass()

# ---------------- Layout.h ----------------
L = ['// GENERATED by tools/gen_assets.py - do not edit', '#pragma once', 'namespace layout {',
     f'constexpr int W = {LW}, H = {LH}, SCALE = {S};',
     f'constexpr int vuIn[4] = {{{",".join(map(str, VU_IN))}}};',
     f'constexpr int vuOut[4] = {{{",".join(map(str, VU_OUT))}}};',
     f'constexpr float vuPivotX = {VU_PIVOT[0]}, vuPivotY = {VU_PIVOT[1]}, vuNeedleLen = {VU_R - 6}, vuAngle = {VU_ANG}f, vuMinDb = {VU_MIN}f, vuMaxDb = {VU_MAX}f;',
     f'constexpr int reelL[2] = {{{REEL_L[0]},{REEL_L[1]}}}, reelR[2] = {{{REEL_R[0]},{REEL_R[1]}}}, reelRadius = {REEL_R_PX};',
     f'constexpr int reelWin[4] = {{{",".join(map(str, REEL_WIN))}}};',
     f'constexpr int keyW = {KEY_W}, keyH = {KEY_H}, keyY = {KEY_Y};',
     f'constexpr int knobY = {KNOB_Y}, knobD = {KNOB_D};',
     f'constexpr float knobX0 = {KNOB_X0}.0f, knobDX = {KNOB_DX}f;',
     f'constexpr int plate[4] = {{{",".join(map(str, PLATE))}}};',
     f'constexpr int numKeys = {len(KEYS)}, numKnobs = {len(KNOBS)};',
     'struct KeyDef { const char* id; const char* asset; };',
     'inline const KeyDef keys[] = {']
for i, (k, t, s_) in enumerate(KEYS):
    L.append(f'    {{"{k}", "cap_{k}"}},')
L.append('};')
L.append('inline const char* const knobIds[] = {' + ','.join(f'"{k[0]}"' for k in KNOBS) + '};')
L.append(f'inline constexpr int keyX0 = {KEY_X0}, keyDX = {KEY_DX};')
L.append('}')
open(f'{SRC}/Layout.h', 'w').write('\n'.join(L) + '\n')
print('ok')
