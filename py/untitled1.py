import cv2
import numpy as np
from numba import jit

@jit(nopython=True)
def dpid_downscale(img, out_w, out_h, lambda_=1.0):
    ih = img.shape[0]
    iw = img.shape[1]
    img = img.astype(np.float64)

    pw, ph = iw / out_w, ih / out_h

    avg_img = np.zeros((out_h, out_w))
    for py in range(out_h):
        for px in range(out_w):
            sx, ex = px * pw, min((px + 1) * pw, iw)
            sy, ey = py * ph, min((py + 1) * ph, ih)
            sxr, exr = int(sx), int(np.ceil(ex))
            syr, eyr = int(sy), int(np.ceil(ey))

            acc, wsum = 0.0, 0.0
            for iy in range(syr, eyr):
                for ix in range(sxr, exr):
                    f = 1.0
                    if ix < sx: f *= 1.0 - (sx - ix)
                    if ix + 1 > ex: f *= 1.0 - (ix + 1 - ex)
                    if iy < sy: f *= 1.0 - (sy - iy)
                    if iy + 1 > ey: f *= 1.0 - (iy + 1 - ey)
                    acc += img[iy, ix] * f
                    wsum += f
            avg_img[py, px] = acc / wsum

    kernel = np.array([[1.0, 2.0, 1.0], [2.0, 4.0, 2.0], [1.0, 2.0, 1.0]])
    out_img = np.zeros((out_h, out_w))

    for py in range(out_h):
        for px in range(out_w):
            acc, wsum = 0.0, 0.0
            for dy in range(-1, 2):
                for dx in range(-1, 2):
                    ny, nx = py + dy, px + dx
                    if 0 <= ny < out_h and 0 <= nx < out_w:
                        w = kernel[dy + 1, dx + 1]
                        acc += avg_img[ny, nx] * w
                        wsum += w
            avg = acc / wsum

            sx, ex = px * pw, min((px + 1) * pw, iw)
            sy, ey = py * ph, min((py + 1) * ph, ih)
            sxr, exr = int(sx), int(np.ceil(ex))
            syr, eyr = int(sy), int(np.ceil(ey))

            acc, wsum = 0.0, 0.0
            for iy in range(syr, eyr):
                for ix in range(sxr, exr):
                    diff = abs(avg - img[iy, ix])
                    f = diff ** lambda_ if lambda_ != 0 else 1.0
                    if ix < sx: f *= 1.0 - (sx - ix)
                    if ix + 1 > ex: f *= 1.0 - (ix + 1 - ex)
                    if iy < sy: f *= 1.0 - (sy - iy)
                    if iy + 1 > ey: f *= 1.0 - (iy + 1 - ey)
                    acc += img[iy, ix] * f
                    wsum += f
            out_img[py, px] = acc / wsum if wsum > 0 else avg

    return out_img

img = cv2.imread(r"C:\smusic\satie\Trois_Gymnopedies_1.bmp", cv2.IMREAD_GRAYSCALE)
h, w = img.shape[:2]
scale = 0.5
result = dpid_downscale(img, int(w * scale), int(h * scale), lambda_=1.0)
result = np.clip(result, 0, 255).astype(np.uint8)
cv2.imshow("dpid", result)
cv2.waitKey(0)
