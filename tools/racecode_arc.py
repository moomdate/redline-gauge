"""RACE CODE rev arc: a procedurally drawn segmented LED bar over the whole sweep.

    build_arc(lit, CX, CY, R_IN, R_OUT, A0, A1) -> (unlit, litband)

`lit` is the 320x240 plate (float RGB) with the mockup's own, partial, lit arc still on it.
The mockup's arc is switched off / painted over and replaced by a clean one:

  unlit    dark track with faint segment outlines,
  litband  every segment lit, colour ramp along the sweep, soft glow.

Angles: degrees, 0 = up, clockwise positive, measured around (CX, CY) like the firmware does.
Only pixels with R_IN <= r <= R_OUT and A0-4 <= angle <= A1+4 are ever changed, and unlit and
litband are identical everywhere the firmware does not swap pixels.

The dial in the mockup is not a circle around (CX, CY): the track runs inside the chrome bezel
on the left (r ~100) and closer in on the right (r ~85). CENTRELINE holds that path as measured
on the mockup, so the bar sits in the dial's own groove instead of across the chrome.
"""
import numpy as np
import cv2

SS = 4                     # supersampling per axis (anti-aliasing)
HALF_W = 3.6               # half width of a segment (px)
SLOT = 1.1                 # dark slot around the segments (px)
NSEG = 54                  # segments over [A0-4, A1+4] -- same slices the firmware redraws
GAP_PX = 1.5               # gap between segments (px along the arc)
GLOW_SIGMA = 2.6           # px
GLOW_GAIN = 0.80
FLARE_A = (122.0, 131.0)   # the spark burst at the redline end: the track fades into it

# (angle, radius of the track centre) measured on the mockup. Left of -40 deg: the groove just
# inside the chrome bezel; right of it: the middle of the mockup's lit band.
CENTRELINE = [
    (-141, 96.8), (-135, 97.6), (-129, 98.4), (-123, 99.0), (-117, 99.6), (-111, 100.0),
    (-105, 100.2), (-99, 100.3), (-93, 100.3), (-87, 100.2), (-81, 99.8), (-75, 99.0),
    (-69, 97.9), (-63, 97.0), (-57, 96.0), (-51, 94.8), (-45, 94.0), (-39, 93.6),
    (-33, 93.6), (-27, 93.8), (-21, 93.8), (-15, 93.4), (-9, 92.8), (-3, 92.0), (3, 91.0),
    (9, 90.2), (15, 89.6), (21, 88.8), (27, 87.8), (33, 86.6), (39, 85.8), (45, 85.2),
    (51, 84.6), (57, 84.4), (63, 84.4), (69, 84.8), (75, 85.6), (81, 86.6), (87, 87.4),
    (93, 88.2), (99, 88.6), (105, 88.6), (111, 88.4), (117, 88.2), (123, 88.0), (129, 88.0),
    (135, 88.0), (141, 88.0),
]

# colour along the sweep (fraction 0..1 of [A0, A1]) -- mockup style: hot yellow, orange, red
RAMP = [
    (0.00, (255, 226, 92)),
    (0.35, (255, 212, 56)),
    (0.55, (255, 176, 30)),
    (0.68, (255, 118, 16)),
    (0.79, (255, 56, 12)),
    (0.88, (250, 22, 16)),
    (1.00, (250, 16, 24)),
]
OFF_FILL = np.array([23.0, 24.0, 28.0])      # unlit segment body
OFF_RIM = np.array([64.0, 66.0, 74.0])       # its faint outline
SLOT_RGB = np.array([5.0, 5.0, 7.0])


def _smooth(e0, e1, x):
    t = np.clip((x - e0) / float(e1 - e0), 0.0, 1.0)
    return t * t * (3 - 2 * t)


def centre_radius(a):
    """Track centre radius at angle(s) a (deg) -- smoothed interpolation of CENTRELINE."""
    ka = np.array([p[0] for p in CENTRELINE], float)
    kr = np.array([p[1] for p in CENTRELINE], float)
    fine = np.arange(ka[0], ka[-1] + 0.01, 0.5)
    rf = np.interp(fine, ka, kr)
    k = np.exp(-0.5 * (np.arange(-24, 25) / 8.0) ** 2)          # sigma 4 deg
    rf = np.convolve(np.pad(rf, 24, mode="edge"), k / k.sum(), mode="valid")
    return np.interp(a, fine, rf)


def _ramp(t):
    xs = [p[0] for p in RAMP]
    cols = np.array([p[1] for p in RAMP], float)
    return np.stack([np.interp(t, xs, cols[:, i]) for i in range(3)], -1)


def _down(x):
    h, w = x.shape[0] // SS, x.shape[1] // SS
    if x.ndim == 2:
        return x.reshape(h, SS, w, SS).mean((1, 3))
    return x.reshape(h, SS, w, SS, x.shape[2]).mean((1, 3))


def build_arc(lit, CX, CY, R_IN, R_OUT, A0, A1):
    lit = np.asarray(lit, float)
    H, W = lit.shape[:2]

    # ---- geometry, supersampled ---------------------------------------------------------------
    ys, xs = np.mgrid[0:H * SS, 0:W * SS]
    px = (xs + 0.5) / SS - 0.5
    py = (ys + 0.5) / SS - 0.5
    r = np.hypot(px - CX, py - CY)
    a = np.degrees(np.arctan2(px - CX, -(py - CY)))
    d = r - centre_radius(a)                    # signed distance from the track centre line
    ad = np.abs(d)

    s0, s1 = A0 - 4.0, A1 + 4.0                 # the sweep the firmware slices into NSEG
    seg_deg = (s1 - s0) / NSEG
    idx = np.floor((a - s0) / seg_deg)
    u = (a - s0) - idx * seg_deg
    px_per_deg = np.radians(1.0) * r
    edge = np.minimum(u, seg_deg - u) * px_per_deg      # px to the nearest segment boundary
    in_sweep = (a >= s0) & (a <= s1)

    seg_hi = (in_sweep & (ad <= HALF_W) & (edge >= GAP_PX / 2)).astype(float)
    slot_hi = (in_sweep & (ad <= HALF_W + SLOT)).astype(float)
    # faint outline: the outermost ~0.9 px of each segment
    rim_hi = seg_hi * np.clip(np.maximum((ad - (HALF_W - 0.9)) / 0.9,
                                         1 - (edge - GAP_PX / 2) / 0.9), 0, 1)
    # lit segment colour: one colour per segment, hotter in the middle of the bar
    seg_mid = s0 + (np.clip(idx, 0, NSEG - 1) + 0.5) * seg_deg
    col = _ramp(np.clip((seg_mid - A0) / (A1 - A0), 0, 1))
    core = np.clip(1 - (d / HALF_W) ** 2, 0, 1)[..., None]
    hotter = np.minimum(col + np.array([0.0, 70.0, 45.0]), 255.0)   # same hue, hotter
    lit_col = col * (0.84 + 0.16 * core) + (hotter - col) * core ** 2
    off_col = OFF_FILL + 0.07 * col
    off_col = off_col + (OFF_RIM - OFF_FILL) * rim_hi[..., None] * 0.8

    seg = _down(seg_hi)                                  # coverage 0..1 at 320x240
    slot = _down(slot_hi)
    lit_rgb = _down(lit_col * seg_hi[..., None]) / np.maximum(seg, 1e-6)[..., None]
    off_rgb = _down(off_col * seg_hi[..., None]) / np.maximum(seg, 1e-6)[..., None]
    glow_src = _down(col * seg_hi[..., None])

    # ---- low-res helpers ----------------------------------------------------------------------
    yy, xx = np.mgrid[0:H, 0:W]
    rr = np.hypot(xx - CX, yy - CY)
    aa = np.degrees(np.arctan2(xx - CX, -(yy - CY)))
    dd = np.abs(rr - centre_radius(aa))
    band = (rr >= R_IN) & (rr <= R_OUT) & (aa >= s0) & (aa <= s1)
    # every change fades out before the band's edges, so nothing is clipped there
    lim = (_smooth(R_IN, R_IN + 2.0, rr) * (1 - _smooth(R_OUT - 2.0, R_OUT, rr)) *
           _smooth(s0, s0 + 1.5, aa) * (1 - _smooth(s1 - 1.5, s1, aa)))
    lim = lim * band
    flare = _smooth(FLARE_A[0], FLARE_A[1], aa)          # 1 inside the spark burst
    keep = (lim * (1 - flare))[..., None]

    # ---- unlit plate --------------------------------------------------------------------------
    # 1. switch the mockup's own lit arc off: right of ~-40 deg, dim what glows around the track
    R_, G_, B_ = lit[..., 0], lit[..., 1], lit[..., 2]
    lum = lit.mean(-1)
    warm = np.clip((R_ - B_ - 25) / 50.0, 0, 1)
    hot = np.clip((lum - 150) / 60.0, 0, 1)              # white-hot core of the mockup band
    zone = _smooth(-47.0, -36.0, aa)
    near = 1 - _smooth(HALF_W + 9.0, HALF_W + 15.0, dd)
    w_off = zone * near * np.maximum(warm, hot)
    w_off = np.maximum(w_off, zone * (1 - _smooth(HALF_W + 2.0, HALF_W + 5.0, dd)))
    dim = (lum[..., None] * np.array([0.16, 0.09, 0.09]) + lit * np.array([0.10, 0.05, 0.05])
           + np.array([4.0, 3.0, 4.0]))
    unlit = lit + (dim - lit) * (w_off[..., None] * keep)
    # 2. the off track: dark slot, then the segments with their outline
    unlit = unlit + (SLOT_RGB - unlit) * (slot[..., None] * keep)
    unlit = unlit + (off_rgb - unlit) * (seg[..., None] * keep)

    # ---- lit band -----------------------------------------------------------------------------
    glow = cv2.GaussianBlur(glow_src, (0, 0), GLOW_SIGMA) * GLOW_GAIN
    glow = glow + cv2.GaussianBlur(glow_src, (0, 0), GLOW_SIGMA * 2.2) * 0.55
    glow = glow * lim[..., None]
    litband = 255.0 - (255.0 - unlit) * (1 - np.clip(glow, 0, 255) / 255.0)     # screen
    seg_a = (seg * lim)[..., None]
    litband = litband + (np.maximum(lit_rgb, litband * flare[..., None]) - litband) * seg_a

    # outside the band nothing may differ from the source plate
    out = ~band
    unlit[out] = lit[out]
    litband[out] = lit[out]
    return np.clip(unlit, 0, 255), np.clip(litband, 0, 255)
