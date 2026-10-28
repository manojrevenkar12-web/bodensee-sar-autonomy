"""Synthetic camera frames of one UAV pass: a strip of lake under the camera with waves, whitecaps,
sun glint and, maybe, the object. Daylight frames are colour (EO camera); night frames are
thermal. SYNTHETIC: the scene model is simple and its parameters are assumptions, chosen so the
detector faces the problems a real one does (small targets, clutter that grows with the wind,
resolution that falls off away from the track)."""

from __future__ import annotations

from dataclasses import dataclass
from typing import Any

import cv2
import numpy as np

# Camera geometry (ASSUMPTION): 60 m altitude, 100 m swath, 512 px across.
ALTITUDE_M = 60.0
SWATH_M = 100.0
FRAME_PX = 512
GSD_M = SWATH_M / FRAME_PX

# Visible size (m) and contrast of each object, in daylight and in thermal.
OBJECTS: dict[str, dict[str, Any]] = {
    "piw": {"size": (0.5, 0.5), "eo": (255, 110, 20), "thermal_dk": 9.0},  # head and shoulders, orange vest
    "sup_board": {"size": (3.0, 0.8), "eo": (240, 240, 235), "thermal_dk": 6.0},  # board, person on it
    "kayak_capsized": {"size": (3.6, 0.6), "eo": (230, 200, 40), "thermal_dk": 4.0},  # hull; person clings
    "dinghy_capsized": {"size": (4.0, 1.6), "eo": (245, 245, 245), "thermal_dk": 3.0},
}


@dataclass(frozen=True)
class Scene:
    sensor: str  # "eo" or "thermal"
    obj: str
    wind: float  # m/s
    offset_m: float  # lateral distance of the object from the track
    present: bool = True


def _waves(rng: np.random.Generator, wind: float) -> np.ndarray:
    """Wave texture: a few random sinusoids plus fine noise, amplitude growing with the wind."""
    yy, xx = np.mgrid[0:FRAME_PX, 0:FRAME_PX].astype(np.float32)
    field = np.zeros((FRAME_PX, FRAME_PX), np.float32)
    for _ in range(6):
        k = rng.uniform(0.02, 0.12)
        ang = rng.uniform(0, np.pi)
        field += np.sin(k * (np.cos(ang) * xx + np.sin(ang) * yy) + rng.uniform(0, 2 * np.pi)).astype(np.float32)
    field /= 6.0
    noise = cv2.GaussianBlur(rng.normal(0, 1, (FRAME_PX, FRAME_PX)).astype(np.float32), (0, 0), 1.2)
    amp = 0.25 + 0.12 * wind
    return amp * (field + 0.6 * noise)


def _whitecaps(rng: np.random.Generator, wind: float) -> list[tuple[int, int, int, int]]:
    """Whitecaps start around 3-4 m/s and grow quickly with the wind."""
    n = int(rng.poisson(max(0.0, (wind - 3.5)) ** 1.6 * 2.0))
    caps = []
    for _ in range(n):
        x, y = int(rng.integers(0, FRAME_PX)), int(rng.integers(0, FRAME_PX))
        caps.append((x, y, int(rng.integers(2, 9)), int(rng.integers(1, 4))))
    return caps


def _target_px(scene: Scene) -> tuple[float, float]:
    # the frame's horizontal axis is across track; the track runs down the middle
    return FRAME_PX / 2 + scene.offset_m / GSD_M, FRAME_PX / 2


def render(scene: Scene, rng: np.random.Generator) -> np.ndarray:
    """One frame: uint8 BGR for daylight, uint8 single channel for thermal."""
    waves = _waves(rng, scene.wind)
    # resolution falls with the viewing angle: blur grows away from the track
    off_nadir = np.arctan2(abs(scene.offset_m), ALTITUDE_M)
    blur = 0.6 + 2.2 * (1.0 / np.cos(off_nadir) ** 2 - 1.0)
    # and so does contrast: longer path through haze, more surface reflection
    contrast = float(np.cos(off_nadir) ** 4)
    cx, cy = _target_px(scene)
    o = OBJECTS[scene.obj]
    w_px, h_px = o["size"][0] / GSD_M, o["size"][1] / GSD_M
    angle = float(rng.uniform(0, 180))
    # waves hide a head in the water part of the time, more often in a fresh breeze
    occluded = scene.obj == "piw" and rng.uniform() < min(0.6, 0.025 * scene.wind**1.3)
    visible = scene.present and abs(scene.offset_m) < SWATH_M / 2 and not occluded
    if scene.sensor == "thermal":
        img: Any = 120.0 + 10.0 * waves  # water at about 15 C, mapped to grey levels
        if visible:
            mask: Any = np.zeros_like(img)
            cv2.ellipse(  # type: ignore[call-overload]
                mask, (int(cx), int(cy)), (max(1, int(w_px / 2)), max(1, int(h_px / 2))), angle, 0, 360, 1.0, -1
            )
            img += mask * o["thermal_dk"] * 6.0 * contrast
        img = cv2.GaussianBlur(img, (0, 0), blur)
        img += rng.normal(0, 3.0, img.shape)  # sensor noise (NETD)
        return np.clip(img, 0, 255).astype(np.uint8)
    base = np.array([95, 85, 45], np.float32)  # lake green-blue, BGR
    img = np.empty((FRAME_PX, FRAME_PX, 3), np.float32)  # type: ignore[no-redef]
    for c in range(3):
        img[..., c] = base[c] + 28.0 * waves
    for x, y, a, b in _whitecaps(rng, scene.wind):
        cv2.ellipse(img, (x, y), (a, b), float(rng.uniform(0, 180)), 0, 360, (235, 235, 235), -1)
    if rng.uniform() < 0.25:  # sun glint patch
        gx, gy = int(rng.integers(0, FRAME_PX)), int(rng.integers(0, FRAME_PX))
        cv2.circle(img, (gx, gy), int(rng.integers(15, 60)), (250, 250, 250), -1)
    if visible:
        bgr = np.array((o["eo"][2], o["eo"][1], o["eo"][0]), np.float32)
        mask = np.zeros((FRAME_PX, FRAME_PX), np.float32)  # type: ignore[no-redef]
        cv2.ellipse(mask, (int(cx), int(cy)), (max(1, int(w_px / 2)), max(1, int(h_px / 2))), angle, 0, 360, 1.0, -1)  # type: ignore[call-overload]
        alpha = (mask * contrast)[..., None]
        img = img * (1.0 - alpha) + bgr[None, None, :] * alpha
    img = cv2.GaussianBlur(img, (0, 0), blur)
    img += rng.normal(0, 4.0, img.shape)
    return np.clip(img, 0, 255).astype(np.uint8)
