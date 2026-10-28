"""The on-board detector (OpenCV): subtract a smooth background, threshold what stands out, keep
blobs of a plausible size, reject what looks like whitecaps or glint. Returns blob centres."""

from __future__ import annotations

from dataclasses import dataclass

import cv2
import numpy as np

from sar.perception.render import GSD_M


@dataclass(frozen=True)
class Detection:
    x_px: float
    y_px: float
    area_px: float
    score: float


MIN_AREA_M2 = 0.12
MAX_AREA_M2 = 12.0
CHROMA_FLOOR = 20.0  # Lab units: whitecaps and glint differ from the water by less


def _blobs(saliency: np.ndarray, k_sigma: float, min_area_m2: float = MIN_AREA_M2, floor: float = 0.0) -> list[Detection]:
    mu, sd = float(saliency.mean()), float(saliency.std()) + 1e-6
    binary = (saliency > max(mu + k_sigma * sd, floor)).astype(np.uint8)
    binary = cv2.morphologyEx(binary, cv2.MORPH_OPEN, np.ones((2, 2), np.uint8))
    n, _labels, stats, centroids = cv2.connectedComponentsWithStats(binary, connectivity=8)
    out = []
    px_area = GSD_M * GSD_M
    for i in range(1, n):
        area = stats[i, cv2.CC_STAT_AREA] * px_area
        if min_area_m2 <= area <= MAX_AREA_M2:
            x0, y0, w, h = stats[i, :4]
            peak = float(saliency[y0 : y0 + h, x0 : x0 + w].max())
            out.append(Detection(float(centroids[i][0]), float(centroids[i][1]), float(area), (peak - mu) / sd))
    return out


def detect_thermal(img: np.ndarray) -> list[Detection]:
    f = img.astype(np.float32)
    background = cv2.medianBlur(img, 31).astype(np.float32)
    saliency = cv2.GaussianBlur(f - background, (0, 0), 1.0)  # warm only
    return _blobs(saliency, 5.0)


def detect_eo(img: np.ndarray) -> list[Detection]:
    lab = cv2.cvtColor(img, cv2.COLOR_BGR2LAB)
    light = lab[..., 0].astype(np.float32)
    light_bg = cv2.medianBlur(lab[..., 0], 31).astype(np.float32)
    a, b = lab[..., 1].astype(np.float32), lab[..., 2].astype(np.float32)
    a_bg = cv2.medianBlur(lab[..., 1], 31).astype(np.float32)
    b_bg = cv2.medianBlur(lab[..., 2], 31).astype(np.float32)
    # coloured things (vests, hulls) differ from the water in chroma; waves, whitecaps and glint
    # mostly change brightness, so they barely register here
    chroma = np.sqrt((a - a_bg) ** 2 + (b - b_bg) ** 2)
    coloured = _blobs(cv2.GaussianBlur(chroma, (0, 0), 1.0), 6.0, floor=CHROMA_FLOOR)
    # white hulls stand out in lightness, as do whitecaps: only larger blobs, at a higher threshold
    bright = _blobs(cv2.GaussianBlur(np.maximum(light - light_bg, 0), (0, 0), 1.0), 7.0, min_area_m2=1.5)
    return coloured + bright


def detect(img: np.ndarray, sensor: str) -> list[Detection]:
    return detect_thermal(img) if sensor == "thermal" else detect_eo(img)
