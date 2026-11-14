#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""
ByAzen — In-Game Minimalist Esports Macro Overlay for Roblox (Blade Ball)
No Alt-Tab Transparent TopMost Overlay (Windows 10 / 11)

Features:
  - True Click-Through Win32 Layered Overlay (WS_EX_LAYERED | WS_EX_TRANSPARENT | WS_EX_NOACTIVATE)
  - Toggle Settings Menu directly over Roblox via [END] (customizable) without Alt-Tab
  - Draggable Top Status HUD (ByAzen | ACTIVE | CPS | Ping ms | FPS) & Keystroke Pill ([F] [M4])
  - Hold [ALT] (customizable) for 4-Slice Radial Quick-Profile Wheel (Low Ping, Clash Pro, Safe, Custom)
  - Full In-Menu Rebinding for Trigger Key/Mouse (M4, M5, RMB, etc.) and Spam Output (F, LMB, etc.)
  - High-Precision 1ms WinMM + SendInput Hardware Scan-Code Macro Engine with Ping Sync & Roblox Focus Guard
"""

from __future__ import annotations

import ctypes
from ctypes import wintypes
import json
import math
import os
import random
import sys
import threading
import time
from pathlib import Path
from typing import Dict, Optional, Tuple

from PyQt6.QtCore import (
    QEasingCurve,
    QPoint,
    QPointF,
    QRect,
    QRectF,
    QSize,
    Qt,
    QTimer,
    QVariantAnimation,
    pyqtSignal,
)
from PyQt6.QtGui import (
    QAction,
    QBrush,
    QColor,
    QCursor,
    QFont,
    QIcon,
    QPainter,
    QPainterPath,
    QPen,
    QPixmap,
    QPolygonF,
)
from PyQt6.QtWidgets import (
    QApplication,
    QFrame,
    QHBoxLayout,
    QLabel,
    QPushButton,
    QStackedWidget,
    QSystemTrayIcon,
    QVBoxLayout,
    QWidget,
)

IS_WINDOWS = sys.platform == "win32"


# ============================================================================
# Virtual Key Table & Human-Readable Esports Key Names
# ============================================================================

VK_NAMES: Dict[int, str] = {
    0x01: "LMB",
    0x02: "RMB",
    0x04: "MMB",
    0x05: "M4",
    0x06: "M5",
    0x08: "BKSP",
    0x09: "TAB",
    0x0D: "ENTER",
    0x10: "SHIFT",
    0xA0: "LSHIFT",
    0xA1: "RSHIFT",
    0x11: "CTRL",
    0xA2: "LCTRL",
    0xA3: "RCTRL",
    0x12: "ALT",
    0xA4: "LALT",
    0xA5: "RALT",
    0x14: "CAPS",
    0x1B: "ESC",
    0x20: "SPACE",
    0x21: "PGUP",
    0x22: "PGDN",
    0x23: "END",
    0x24: "HOME",
    0x25: "LEFT",
    0x26: "UP",
    0x27: "RIGHT",
    0x28: "DOWN",
    0x2D: "INS",
    0x2E: "DEL",
    0xBF: "/",
    0xC0: "~",
    0xBD: "-",
    0xBB: "=",
    0xDB: "[",
    0xDD: "]",
    0xBA: ";",
    0xDE: "'",
    0xBC: ",",
    0xBE: ".",
}

for _digit in range(0x30, 0x3A):
    VK_NAMES[_digit] = chr(_digit)
for _letter in range(0x41, 0x5B):
    VK_NAMES[_letter] = chr(_letter)
for _fn in range(1, 13):
    VK_NAMES[0x70 + _fn - 1] = f"F{_fn}"

EXTENDED_VKS = {0x21, 0x22, 0x23, 0x24, 0x25, 0x26, 0x27, 0x28, 0x2D, 0x2E, 0xA3, 0xA5}

CAPTURE_CANDIDATE_VKS = [
    0x01, 0x02, 0x04, 0x05, 0x06,
    0x09, 0x10, 0x11, 0x12, 0x14, 0x20,
    0x21, 0x22, 0x23, 0x24, 0x2D, 0x2E,
    *range(0x30, 0x3A),
    *range(0x41, 0x5B),
    *range(0x70, 0x7C),
    0xC0, 0xBD, 0xBB, 0xDB, 0xDD, 0xBA, 0xBC, 0xBE, 0xBF,
]


def vk_to_name(vk: int) -> str:
    return VK_NAMES.get(vk, f"VK_{vk:02X}")


# ============================================================================
# Win32 Native Overlay & SendInput Engine (Zero Memory Touch, Pure OS API)
# ============================================================================

if IS_WINDOWS:
    ULONG_PTR = ctypes.c_uint64 if ctypes.sizeof(ctypes.c_void_p) == 8 else ctypes.c_uint32

    class MOUSEINPUT(ctypes.Structure):
        _fields_ = [
            ("dx", wintypes.LONG),
            ("dy", wintypes.LONG),
            ("mouseData", wintypes.DWORD),
            ("dwFlags", wintypes.DWORD),
            ("time", wintypes.DWORD),
            ("dwExtraInfo", ULONG_PTR),
        ]

    class KEYBDINPUT(ctypes.Structure):
        _fields_ = [
            ("wVk", wintypes.WORD),
            ("wScan", wintypes.WORD),
            ("dwFlags", wintypes.DWORD),
            ("time", wintypes.DWORD),
            ("dwExtraInfo", ULONG_PTR),
        ]

    class HARDWAREINPUT(ctypes.Structure):
        _fields_ = [
            ("uMsg", wintypes.DWORD),
            ("wParamL", wintypes.WORD),
            ("wParamH", wintypes.WORD),
        ]

    class _INPUT_UNION(ctypes.Union):
        _fields_ = [
            ("mi", MOUSEINPUT),
            ("ki", KEYBDINPUT),
            ("hi", HARDWAREINPUT),
        ]

    class INPUT(ctypes.Structure):
        _fields_ = [
            ("type", wintypes.DWORD),
            ("union", _INPUT_UNION),
        ]


class Win32Bridge:
    """Pure Win32 window transparency, focus helper, and hardware scan-code SendInput."""

    GWL_EXSTYLE = -20
    WS_EX_TRANSPARENT = 0x00000020
    WS_EX_TOOLWINDOW = 0x00000080
    WS_EX_TOPMOST = 0x00000008
    WS_EX_LAYERED = 0x00080000
    WS_EX_NOACTIVATE = 0x08000000

    SWP_NOMOVE = 0x0002
    SWP_NOSIZE = 0x0001
    SWP_NOACTIVATE = 0x0010
    SWP_FRAMECHANGED = 0x0020
    HWND_TOPMOST = -1

    INPUT_MOUSE = 0
    INPUT_KEYBOARD = 1

    KEYEVENTF_EXTENDEDKEY = 0x0001
    KEYEVENTF_KEYUP = 0x0002
    KEYEVENTF_SCANCODE = 0x0008

    MOUSEEVENTF_LEFTDOWN = 0x0002
    MOUSEEVENTF_LEFTUP = 0x0004
    MOUSEEVENTF_RIGHTDOWN = 0x0008
    MOUSEEVENTF_RIGHTUP = 0x0010
    MOUSEEVENTF_MIDDLEDOWN = 0x0020
    MOUSEEVENTF_MIDDLEUP = 0x0040

    MAPVK_VK_TO_VSC = 0

    def __init__(self) -> None:
        self.last_roblox_hwnd: int = 0
        if IS_WINDOWS:
            self.user32 = ctypes.windll.user32
            self.winmm = ctypes.windll.winmm
            try:
                self.winmm.timeBeginPeriod(1)
            except Exception:
                pass
        else:
            self.user32 = None
            self.winmm = None

    def cleanup(self) -> None:
        if IS_WINDOWS and self.winmm:
            try:
                self.winmm.timeEndPeriod(1)
            except Exception:
                pass

    @staticmethod
    def enable_dpi_awareness() -> None:
        if not IS_WINDOWS:
            return
        try:
            ctypes.windll.shcore.SetProcessDpiAwareness(2)
        except Exception:
            try:
                ctypes.windll.user32.SetProcessDPIAware()
            except Exception:
                pass

    def apply_overlay_style(self, hwnd: int, click_through: bool) -> None:
        if not IS_WINDOWS or not hwnd:
            return
        style = self.user32.GetWindowLongW(hwnd, self.GWL_EXSTYLE)
        style |= self.WS_EX_LAYERED | self.WS_EX_TOOLWINDOW
        if click_through:
            style |= self.WS_EX_TRANSPARENT | self.WS_EX_NOACTIVATE
        else:
            style &= ~self.WS_EX_TRANSPARENT
            style &= ~self.WS_EX_NOACTIVATE
        self.user32.SetWindowLongW(hwnd, self.GWL_EXSTYLE, style)
        self.user32.SetWindowPos(
            wintypes.HWND(hwnd),
            wintypes.HWND(self.HWND_TOPMOST),
            0, 0, 0, 0,
            self.SWP_NOMOVE | self.SWP_NOSIZE | self.SWP_NOACTIVATE | self.SWP_FRAMECHANGED,
        )

    def is_vk_down(self, vk: int) -> bool:
        if not IS_WINDOWS or vk <= 0:
            return False
        # Treat generic Shift/Ctrl/Alt properly alongside left/right variants
        if vk == 0x12:
            return bool(
                (self.user32.GetAsyncKeyState(0x12) & 0x8000)
                or (self.user32.GetAsyncKeyState(0xA4) & 0x8000)
                or (self.user32.GetAsyncKeyState(0xA5) & 0x8000)
            )
        return bool(self.user32.GetAsyncKeyState(vk) & 0x8000)

    def get_cursor_pos(self) -> Tuple[int, int]:
        if not IS_WINDOWS:
            pos = QCursor.pos()
            return pos.x(), pos.y()
        pt = wintypes.POINT()
        self.user32.GetCursorPos(ctypes.byref(pt))
        return int(pt.x), int(pt.y)

    def get_foreground_title(self) -> str:
        if not IS_WINDOWS:
            return "Roblox"
        hwnd = self.user32.GetForegroundWindow()
        if not hwnd:
            return ""
        length = self.user32.GetWindowTextLengthW(hwnd)
        if length <= 0:
            return ""
        buf = ctypes.create_unicode_buffer(length + 1)
        self.user32.GetWindowTextW(hwnd, buf, length + 1)
        title = buf.value
        if "roblox" in title.lower():
            self.last_roblox_hwnd = int(hwnd)
        return title

    def is_roblox_active(self) -> bool:
        if not IS_WINDOWS:
            return True
        title = self.get_foreground_title().lower()
        return "roblox" in title

    def refocus_roblox(self) -> None:
        if not IS_WINDOWS:
            return
        hwnd = self.last_roblox_hwnd
        if not hwnd or not self.user32.IsWindow(hwnd):
            hwnd = self.user32.FindWindowW(None, "Roblox")
        if hwnd:
            try:
                self.user32.SetForegroundWindow(hwnd)
            except Exception:
                pass

    def send_action_down(self, vk: int) -> None:
        if not IS_WINDOWS:
            return
        inp = INPUT()
        if vk == 0x01:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_LEFTDOWN, 0, 0)
        elif vk == 0x02:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_RIGHTDOWN, 0, 0)
        elif vk == 0x04:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_MIDDLEDOWN, 0, 0)
        else:
            scan = self.user32.MapVirtualKeyW(vk, self.MAPVK_VK_TO_VSC)
            flags = self.KEYEVENTF_SCANCODE if scan else 0
            if vk in EXTENDED_VKS:
                flags |= self.KEYEVENTF_EXTENDEDKEY
            inp.type = self.INPUT_KEYBOARD
            inp.union.ki = KEYBDINPUT(0 if scan else vk, scan, flags, 0, 0)
        self.user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))

    def send_action_up(self, vk: int) -> None:
        if not IS_WINDOWS:
            return
        inp = INPUT()
        if vk == 0x01:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_LEFTUP, 0, 0)
        elif vk == 0x02:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_RIGHTUP, 0, 0)
        elif vk == 0x04:
            inp.type = self.INPUT_MOUSE
            inp.union.mi = MOUSEINPUT(0, 0, 0, self.MOUSEEVENTF_MIDDLEUP, 0, 0)
        else:
            scan = self.user32.MapVirtualKeyW(vk, self.MAPVK_VK_TO_VSC)
            flags = (self.KEYEVENTF_SCANCODE if scan else 0) | self.KEYEVENTF_KEYUP
            if vk in EXTENDED_VKS:
                flags |= self.KEYEVENTF_EXTENDEDKEY
            inp.type = self.INPUT_KEYBOARD
            inp.union.ki = KEYBDINPUT(0 if scan else vk, scan, flags, 0, 0)
        self.user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(INPUT))


# ============================================================================
# Persistent Configuration & Profiles Manager
# ============================================================================

def get_config_path() -> Path:
    if getattr(sys, "frozen", False):
        base = Path(sys.executable).resolve().parent
    else:
        base = Path(__file__).resolve().parent
    return base / "byazen_config.json"


DEFAULT_PROFILES: Dict[str, dict] = {
    "Low Ping": {
        "target_cps": 56,
        "delay_ms": 15,
        "hold_ms": 3,
        "jitter_pct": 0,
        "ping_sync": True,
    },
    "Clash Pro": {
        "target_cps": 48,
        "delay_ms": 18,
        "hold_ms": 3,
        "jitter_pct": 0,
        "ping_sync": True,
    },
    "Safe": {
        "target_cps": 26,
        "delay_ms": 34,
        "hold_ms": 5,
        "jitter_pct": 9,
        "ping_sync": False,
    },
    "Custom": {
        "target_cps": 40,
        "delay_ms": 22,
        "hold_ms": 4,
        "jitter_pct": 2,
        "ping_sync": True,
    },
}


DEFAULT_CONFIG: dict = {
    "macro_enabled": True,
    "target_cps": 48,
    "delay_ms": 18,
    "hold_ms": 3,
    "jitter_pct": 0,
    "ping_sync": True,
    "no_alttab_lock": True,
    "only_in_roblox": True,
    "chat_safe_pause": True,
    "activation_mode": "hold",  # "hold" or "toggle"
    "trigger_vk": 0x05,         # M4 (XButton1)
    "output_vk": 0x46,          # F key (can switch to 0x01 LMB in 1 click)
    "menu_vk": 0x23,            # END
    "radial_vk": 0x12,          # ALT
    "panic_vk": 0x79,           # F10
    "active_profile": "Clash Pro",
    "show_hud_bar": True,
    "show_key_pill": True,
    "hud_opacity": 95,
    "positions": {
        "menu": [36, 155],
        "hud_bar": [-1, 36],    # -1 = auto right-align on first run
        "key_pill": [-1, -1],
        "radial": [-1, -1],
    },
    "profiles": DEFAULT_PROFILES,
}


class ConfigStore:
    def __init__(self) -> None:
        self.path = get_config_path()
        self.data: dict = json.loads(json.dumps(DEFAULT_CONFIG))
        self.load()

    def load(self) -> None:
        if not self.path.exists():
            return
        try:
            raw = json.loads(self.path.read_text(encoding="utf-8"))
            if isinstance(raw, dict):
                for k, v in raw.items():
                    if k in ("positions", "profiles") and isinstance(v, dict):
                        self.data[k].update(v)
                    else:
                        self.data[k] = v
        except Exception:
            pass

    def save(self) -> None:
        try:
            self.path.write_text(
                json.dumps(self.data, indent=2, ensure_ascii=False),
                encoding="utf-8",
            )
        except Exception:
            pass


# ============================================================================
# High-Precision Offline Macro Engine
# ============================================================================

def precise_sleep(seconds: float) -> None:
    if seconds <= 0:
        return
    end = time.perf_counter() + seconds
    if seconds > 0.003:
        time.sleep(seconds - 0.002)
    while time.perf_counter() < end:
        pass


class MacroEngine:
    """High-precision background macro loop; no network connection is used."""

    def __init__(self, cfg: ConfigStore, win32: Win32Bridge) -> None:
        self.cfg = cfg
        self.win32 = win32
        self.running = True
        self.is_firing = False
        self.toggle_latched = False
        self.chat_paused = False
        self.live_ping_ms: int = 42  # static fallback; network ping is intentionally disabled
        self.measured_cps: int = 0

        self._click_timestamps: list[float] = []
        self._prev_trigger_down = False
        self._prev_slash_down = False
        self._prev_enter_esc_down = False

        self._macro_thread = threading.Thread(target=self._macro_loop, daemon=True)

    def start(self) -> None:
        self._macro_thread.start()

    def stop(self) -> None:
        self.running = False

    def _macro_loop(self) -> None:
        while self.running:
            d = self.cfg.data
            if not d.get("macro_enabled", True):
                self.is_firing = False
                self.toggle_latched = False
                time.sleep(0.01)
                continue

            # Optional Chat Safe guard: '/' pauses until Enter or Esc is pressed
            if d.get("chat_safe_pause", True) and IS_WINDOWS:
                slash_down = self.win32.is_vk_down(0xBF)
                if slash_down and not self._prev_slash_down and self.win32.is_roblox_active():
                    self.chat_paused = True
                self._prev_slash_down = slash_down

                exit_chat = self.win32.is_vk_down(0x0D) or self.win32.is_vk_down(0x1B)
                if exit_chat and not self._prev_enter_esc_down:
                    self.chat_paused = False
                self._prev_enter_esc_down = exit_chat
            else:
                self.chat_paused = False

            trigger_vk = int(d.get("trigger_vk", 0x05))
            output_vk = int(d.get("output_vk", 0x46))

            # Safety guard: prevent self-trigger if user somehow bound identical keys
            if trigger_vk == output_vk:
                self.is_firing = False
                time.sleep(0.02)
                continue

            trig_down = self.win32.is_vk_down(trigger_vk)
            mode = d.get("activation_mode", "hold")

            if mode == "toggle":
                if trig_down and not self._prev_trigger_down:
                    self.toggle_latched = not self.toggle_latched
                should_fire = self.toggle_latched
            else:
                self.toggle_latched = False
                should_fire = trig_down
            self._prev_trigger_down = trig_down

            if should_fire and d.get("only_in_roblox", True):
                if not self.win32.is_roblox_active():
                    should_fire = False

            if self.chat_paused:
                should_fire = False

            if not should_fire:
                self.is_firing = False
                now = time.perf_counter()
                self._click_timestamps = [t for t in self._click_timestamps if now - t <= 1.0]
                self.measured_cps = len(self._click_timestamps)
                time.sleep(0.004)
                continue

            # Execute high-precision click/key cycle
            self.is_firing = True
            hold_ms = max(1, int(d.get("hold_ms", 3)))
            delay_ms = max(2, int(d.get("delay_ms", 18)))
            jitter_pct = int(d.get("jitter_pct", 0))

            # Ping Sync micro-compensation: subtly tightens cadence at higher ping spikes
            if d.get("ping_sync", True):
                ping_offset = max(-3.0, min(4.0, (45 - self.live_ping_ms) * 0.04))
                effective_delay_ms = max(2.0, delay_ms + ping_offset)
            else:
                effective_delay_ms = float(delay_ms)

            if jitter_pct > 0:
                spread = effective_delay_ms * (jitter_pct / 100.0)
                effective_delay_ms = max(2.0, effective_delay_ms + random.uniform(-spread, spread))

            self.win32.send_action_down(output_vk)
            precise_sleep(hold_ms / 1000.0)
            self.win32.send_action_up(output_vk)

            now = time.perf_counter()
            self._click_timestamps.append(now)
            self._click_timestamps = [t for t in self._click_timestamps if now - t <= 1.0]
            self.measured_cps = len(self._click_timestamps)

            precise_sleep(effective_delay_ms / 1000.0)


# ============================================================================
# Custom Minimalist Esports UI Controls (1:1 Concept Visual Styling)
# ============================================================================

COLOR_BG_MAIN = QColor(17, 19, 24, 246)       # #111318
COLOR_BG_ELEVATED = QColor(29, 33, 42, 255)   # #1D212A
COLOR_BORDER = QColor(44, 49, 61, 255)        # #2C313D
COLOR_DIVIDER = QColor(36, 40, 50, 255)       # #242832
COLOR_TEXT = QColor(244, 246, 250, 255)       # #F4F6FA
COLOR_TEXT_DIM = QColor(168, 175, 190, 255)   # #A8AFBE
COLOR_CRIMSON = QColor(229, 57, 69, 255)      # #E53945
COLOR_CRIMSON_BG = QColor(68, 22, 27, 235)    # #44161B
COLOR_GREEN = QColor(62, 212, 112, 255)       # #3ED470


def make_font(size_px: int, weight: int = QFont.Weight.Medium, mono: bool = False) -> QFont:
    families = ["Consolas", "JetBrains Mono", "Courier New", "monospace"] if mono else [
        "Segoe UI", "Inter", "DejaVu Sans", "Arial"
    ]
    font = QFont(families[0])
    font.setFamilies(families)
    font.setPixelSize(size_px)
    font.setWeight(weight)
    return font


class EsportsToggle(QWidget):
    """Minimalist matte slate pill toggle switch with crisp white animated knob."""

    toggled = pyqtSignal(bool)

    def __init__(self, checked: bool = False, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self.setFixedSize(46, 24)
        self.setCursor(Qt.CursorShape.PointingHandCursor)
        self._checked = checked
        self._knob_pos = 1.0 if checked else 0.0
        self._anim = QVariantAnimation(self)
        self._anim.setDuration(130)
        self._anim.setEasingCurve(QEasingCurve.Type.OutCubic)
        self._anim.valueChanged.connect(self._on_anim)

    def _on_anim(self, val: float) -> None:
        self._knob_pos = float(val)
        self.update()

    def isChecked(self) -> bool:
        return self._checked

    def setChecked(self, checked: bool, emit: bool = False) -> None:
        if self._checked == checked:
            return
        self._checked = checked
        self._anim.stop()
        self._anim.setStartValue(self._knob_pos)
        self._anim.setEndValue(1.0 if checked else 0.0)
        self._anim.start()
        if emit:
            self.toggled.emit(self._checked)

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self.setChecked(not self._checked, emit=True)

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(1.0, 1.0, self.width() - 2.0, self.height() - 2.0)

        # Interpolate track color from dark #232732 to sleek slate #5A6070
        t = self._knob_pos
        r = int(35 + (90 - 35) * t)
        g = int(39 + (96 - 39) * t)
        b = int(50 + (112 - 50) * t)
        p.setPen(QPen(QColor(62, 68, 82), 1.0))
        p.setBrush(QColor(r, g, b))
        p.drawRoundedRect(rect, 11.0, 11.0)

        knob_d = 18.0
        x = 4.0 + (self.width() - knob_d - 8.0) * t
        y = (self.height() - knob_d) / 2.0
        knob_color = QColor(250, 251, 255) if self._checked else QColor(150, 156, 170)
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(knob_color)
        p.drawEllipse(QRectF(x, y, knob_d, knob_d))


class EsportsSlider(QWidget):
    """Minimalist slider with inline white title + crimson live value & crisp white track/thumb."""

    valueChanged = pyqtSignal(int)

    def __init__(
        self,
        title: str,
        min_val: int,
        max_val: int,
        val: int,
        suffix: str = "",
        parent: Optional[QWidget] = None,
    ) -> None:
        super().__init__(parent)
        self._title = title
        self._min = min_val
        self._max = max_val
        self._val = max(min_val, min(max_val, val))
        self._suffix = suffix
        self._dragging = False
        self.setFixedHeight(50)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def value(self) -> int:
        return self._val

    def setValue(self, val: int, emit: bool = False) -> None:
        clamped = max(self._min, min(self._max, int(val)))
        if clamped == self._val:
            return
        self._val = clamped
        self.update()
        if emit:
            self.valueChanged.emit(self._val)

    def _pos_to_val(self, x: float) -> int:
        pad = 9.0
        span = max(1.0, self.width() - 2 * pad)
        ratio = max(0.0, min(1.0, (x - pad) / span))
        return int(round(self._min + ratio * (self._max - self._min)))

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            self._dragging = True
            self.setValue(self._pos_to_val(event.position().x()), emit=True)

    def mouseMoveEvent(self, event) -> None:
        if self._dragging:
            self.setValue(self._pos_to_val(event.position().x()), emit=True)

    def mouseReleaseEvent(self, event) -> None:
        self._dragging = False

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        # Label: "Target CPS: " in white + "48" in crimson
        font = make_font(14, QFont.Weight.DemiBold)
        p.setFont(font)
        fm = p.fontMetrics()
        prefix = f"{self._title}: "
        p.setPen(COLOR_TEXT)
        p.drawText(QPointF(2.0, 16.0), prefix)

        val_text = f"{self._val}{self._suffix}"
        p.setPen(COLOR_CRIMSON)
        p.drawText(QPointF(2.0 + fm.horizontalAdvance(prefix), 16.0), val_text)

        # Slider Track
        pad = 9.0
        track_y = 35.0
        track_h = 5.0
        span = max(1.0, self.width() - 2 * pad)
        ratio = (self._val - self._min) / max(1, (self._max - self._min))
        thumb_x = pad + span * ratio

        # Unfilled track
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(QColor(66, 71, 84))
        p.drawRoundedRect(QRectF(pad, track_y, span, track_h), 2.5, 2.5)

        # Filled white track
        if thumb_x > pad:
            p.setBrush(QColor(238, 241, 247))
            p.drawRoundedRect(QRectF(pad, track_y, thumb_x - pad, track_h), 2.5, 2.5)

        # Crisp circular thumb
        thumb_r = 8.5
        p.setBrush(QColor(0, 0, 0, 90))
        p.drawEllipse(QPointF(thumb_x, track_y + track_h / 2.0 + 1.0), thumb_r, thumb_r)
        p.setBrush(QColor(250, 251, 255))
        p.drawEllipse(QPointF(thumb_x, track_y + track_h / 2.0), thumb_r, thumb_r)


def draw_vector_icon(p: QPainter, kind: str, center: QPointF, size: float, color: QColor) -> None:
    """Draws sharp minimalist vector icons matching the ByAzen concept design."""
    p.save()
    p.setRenderHint(QPainter.RenderHint.Antialiasing)
    pen = QPen(color, 1.6, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap, Qt.PenJoinStyle.RoundJoin)
    p.setPen(pen)
    p.setBrush(Qt.BrushStyle.NoBrush)
    cx, cy = center.x(), center.y()
    half = size / 2.0

    if kind == "sliders":
        ys = [cy - half * 0.62, cy, cy + half * 0.62]
        xs = [cx - half * 0.25, cx + half * 0.32, cx - half * 0.15]
        for y, knob_x in zip(ys, xs):
            p.drawLine(QPointF(cx - half * 0.85, y), QPointF(cx + half * 0.85, y))
            p.setBrush( COLOR_BG_MAIN )
            p.drawEllipse(QPointF(knob_x, y), 2.3, 2.3)
            p.setBrush(Qt.BrushStyle.NoBrush)

    elif kind == "target":
        r = half * 0.68
        p.drawEllipse(center, r, r)
        p.drawLine(QPointF(cx, cy - half * 0.95), QPointF(cx, cy - r * 0.55))
        p.drawLine(QPointF(cx, cy + r * 0.55), QPointF(cx, cy + half * 0.95))
        p.drawLine(QPointF(cx - half * 0.95, cy), QPointF(cx - r * 0.55, cy))
        p.drawLine(QPointF(cx + r * 0.55, cy), QPointF(cx + half * 0.95, cy))
        p.setBrush(color)
        p.drawEllipse(center, 1.5, 1.5)

    elif kind == "grid":
        g = half * 0.72
        gap = 2.2
        s = g - gap / 2.0
        for dx in (-g, gap / 2.0):
            for dy in (-g, gap / 2.0):
                p.drawRoundedRect(QRectF(cx + dx, cy + dy, s, s), 1.8, 1.8)

    elif kind == "profile":
        p.drawEllipse(QPointF(cx, cy - half * 0.34), half * 0.34, half * 0.34)
        arc = QPainterPath()
        arc.moveTo(cx - half * 0.72, cy + half * 0.78)
        arc.quadTo(cx, cy + half * 0.05, cx + half * 0.72, cy + half * 0.78)
        p.drawPath(arc)

    elif kind == "keyboard":
        rect = QRectF(cx - half * 0.88, cy - half * 0.56, half * 1.76, half * 1.12)
        p.drawRoundedRect(rect, 2.5, 2.5)
        p.drawLine(QPointF(cx - half * 0.42, cy + half * 0.24), QPointF(cx + half * 0.42, cy + half * 0.24))
        p.setBrush(color)
        p.setPen(Qt.PenStyle.NoPen)
        for kx in (-0.52, -0.18, 0.18, 0.52):
            p.drawEllipse(QPointF(cx + half * kx, cy - half * 0.16), 1.2, 1.2)

    elif kind == "pulse":
        path = QPainterPath()
        path.moveTo(cx - half * 0.95, cy)
        path.lineTo(cx - half * 0.42, cy)
        path.lineTo(cx - half * 0.20, cy - half * 0.65)
        path.lineTo(cx + half * 0.08, cy + half * 0.70)
        path.lineTo(cx + half * 0.30, cy - half * 0.28)
        path.lineTo(cx + half * 0.48, cy)
        path.lineTo(cx + half * 0.95, cy)
        p.drawPath(path)

    elif kind == "crown":
        poly = QPolygonF([
            QPointF(cx - half * 0.72, cy + half * 0.45),
            QPointF(cx - half * 0.85, cy - half * 0.42),
            QPointF(cx - half * 0.32, cy + half * 0.02),
            QPointF(cx, cy - half * 0.62),
            QPointF(cx + half * 0.32, cy + half * 0.02),
            QPointF(cx + half * 0.85, cy - half * 0.42),
            QPointF(cx + half * 0.72, cy + half * 0.45),
        ])
        p.setBrush(color)
        p.drawPolygon(poly)
        p.drawLine(QPointF(cx - half * 0.70, cy + half * 0.68), QPointF(cx + half * 0.70, cy + half * 0.68))

    elif kind == "shield":
        path = QPainterPath()
        path.moveTo(cx, cy - half * 0.82)
        path.lineTo(cx + half * 0.70, cy - half * 0.52)
        path.quadTo(cx + half * 0.68, cy + half * 0.38, cx, cy + half * 0.86)
        path.quadTo(cx - half * 0.68, cy + half * 0.38, cx - half * 0.70, cy - half * 0.52)
        path.closeSubpath()
        p.drawPath(path)

    elif kind == "sword":
        p.setBrush(color)
        # Diagonal katana/sword matching center of Radial Wheel
        blade = QPolygonF([
            QPointF(cx - half * 0.16, cy + half * 0.16),
            QPointF(cx + half * 0.62, cy - half * 0.62),
            QPointF(cx + half * 0.82, cy - half * 0.82),
            QPointF(cx + half * 0.76, cy - half * 0.46),
            QPointF(cx + half * 0.02, cy + half * 0.28),
        ])
        p.drawPolygon(blade)
        p.setPen(QPen(color, 2.4, Qt.PenStyle.SolidLine, Qt.PenCapStyle.RoundCap))
        # Crossguard & hilt
        p.drawLine(QPointF(cx - half * 0.36, cy - half * 0.04), QPointF(cx + half * 0.08, cy + half * 0.40))
        p.drawLine(QPointF(cx - half * 0.14, cy + half * 0.18), QPointF(cx - half * 0.68, cy + half * 0.72))

    p.restore()


class SidebarTabButton(QPushButton):
    """Left navigation button inside ByAzen window."""

    def __init__(self, label: str, icon_kind: str, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._label = label
        self._icon_kind = icon_kind
        self._active = False
        self._hovered = False
        self.setFixedHeight(42)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def setActive(self, active: bool) -> None:
        self._active = active
        self.update()

    def enterEvent(self, event) -> None:
        self._hovered = True
        self.update()

    def leaveEvent(self, event) -> None:
        self._hovered = False
        self.update()

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        rect = QRectF(0, 0, self.width(), self.height())

        if self._active:
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QColor(35, 39, 49))
            p.drawRoundedRect(rect, 6.0, 6.0)
            # Subtle left accent indicator
            p.setBrush(QColor(235, 239, 248))
            p.drawRoundedRect(QRectF(0, 9, 3.0, self.height() - 18), 1.5, 1.5)
        elif self._hovered:
            p.setPen(Qt.PenStyle.NoPen)
            p.setBrush(QColor(26, 29, 37))
            p.drawRoundedRect(rect, 6.0, 6.0)

        col = COLOR_TEXT if (self._active or self._hovered) else QColor(200, 206, 218)
        draw_vector_icon(p, self._icon_kind, QPointF(24.0, self.height() / 2.0), 18.0, col)

        p.setFont(make_font(14, QFont.Weight.DemiBold if self._active else QFont.Weight.Medium))
        p.setPen(col)
        p.drawText(
            QRectF(46.0, 0, self.width() - 52.0, self.height()),
            int(Qt.AlignmentFlag.AlignVCenter | Qt.AlignmentFlag.AlignLeft),
            self._label,
        )


class SegmentedChoice(QWidget):
    """Minimalist pill selector for binary/ternary options inside tabs."""

    changed = pyqtSignal(str)

    def __init__(self, options: list[Tuple[str, str]], current: str, parent: Optional[QWidget] = None) -> None:
        super().__init__(parent)
        self._options = options
        self._current = current
        self.setFixedHeight(32)
        self.setCursor(Qt.CursorShape.PointingHandCursor)

    def setCurrent(self, key: str) -> None:
        self._current = key
        self.update()

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton and self._options:
            seg_w = self.width() / len(self._options)
            idx = max(0, min(len(self._options) - 1, int(event.position().x() / seg_w)))
            key = self._options[idx][0]
            if key != self._current:
                self._current = key
                self.update()
                self.changed.emit(key)

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        outer = QRectF(0.5, 0.5, self.width() - 1.0, self.height() - 1.0)
        p.setPen(QPen(COLOR_BORDER, 1.0))
        p.setBrush(QColor(23, 26, 33))
        p.drawRoundedRect(outer, 6.0, 6.0)

        if not self._options:
            return
        seg_w = (self.width() - 4.0) / len(self._options)
        p.setFont(make_font(12, QFont.Weight.DemiBold))
        for i, (key, label) in enumerate(self._options):
            r = QRectF(2.0 + i * seg_w, 2.0, seg_w, self.height() - 4.0)
            if key == self._current:
                p.setPen(QPen(COLOR_CRIMSON, 1.0))
                p.setBrush(COLOR_CRIMSON_BG)
                p.drawRoundedRect(r, 4.5, 4.5)
                p.setPen(COLOR_TEXT)
            else:
                p.setPen(COLOR_TEXT_DIM)
            p.drawText(r, int(Qt.AlignmentFlag.AlignCenter), label)


# ============================================================================
# Floating Draggable Mini-HUD Widgets & Radial Quick-Profile Wheel
# ============================================================================

class HudStatusBar(QWidget):
    """Top-right draggable bar: ByAzen | ACTIVE | 48 CPS | 38 ms | 240 FPS | [:::]"""

    positionChanged = pyqtSignal(int, int)

    def __init__(self, cfg: ConfigStore, engine: MacroEngine, parent: QWidget) -> None:
        super().__init__(parent)
        self.cfg = cfg
        self.engine = engine
        self.monitor_fps = 240
        self.unlocked = False
        self._drag_offset: Optional[QPoint] = None
        self.setFixedSize(468, 44)

    def setUnlocked(self, unlocked: bool) -> None:
        self.unlocked = unlocked
        self.setCursor(Qt.CursorShape.SizeAllCursor if unlocked else Qt.CursorShape.ArrowCursor)
        self.update()

    def mousePressEvent(self, event) -> None:
        if self.unlocked and event.button() == Qt.MouseButton.LeftButton:
            self._drag_offset = event.globalPosition().toPoint() - self.pos()

    def mouseMoveEvent(self, event) -> None:
        if self.unlocked and self._drag_offset is not None:
            new_pos = event.globalPosition().toPoint() - self._drag_offset
            parent_rect = self.parentWidget().rect()
            nx = max(8, min(parent_rect.width() - self.width() - 8, new_pos.x()))
            ny = max(8, min(parent_rect.height() - self.height() - 8, new_pos.y()))
            self.move(nx, ny)
            self.positionChanged.emit(nx, ny)

    def mouseReleaseEvent(self, event) -> None:
        self._drag_offset = None

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        opacity = max(0.35, min(1.0, self.cfg.data.get("hud_opacity", 95) / 100.0))
        p.setOpacity(opacity)

        rect = QRectF(0.5, 0.5, self.width() - 1.0, self.height() - 1.0)
        border_col = QColor(82, 90, 110) if self.unlocked else COLOR_BORDER
        p.setPen(QPen(border_col, 1.2))
        p.setBrush(QColor(15, 17, 22, 238))
        p.drawRoundedRect(rect, 7.0, 7.0)

        enabled = self.cfg.data.get("macro_enabled", True)
        firing = self.engine.is_firing
        target_cps = int(self.cfg.data.get("target_cps", 48))
        cps_val = self.engine.measured_cps if (firing and self.engine.measured_cps > 0) else target_cps
        ping_val = self.engine.live_ping_ms

        if not enabled:
            status_txt, status_col = "PAUSED", COLOR_TEXT_DIM
        elif firing:
            status_txt, status_col = "CLASH", COLOR_CRIMSON
        else:
            status_txt, status_col = "ACTIVE", COLOR_GREEN

        segments = [
            ("ByAzen", COLOR_TEXT, make_font(15, QFont.Weight.Bold), 92),
            (status_txt, status_col, make_font(13, QFont.Weight.DemiBold), 82),
            (f"{cps_val} CPS", COLOR_TEXT, make_font(13, QFont.Weight.Medium), 82),
            (f"{ping_val} ms", COLOR_CRIMSON, make_font(13, QFont.Weight.DemiBold), 76),
            (f"{self.monitor_fps} FPS", COLOR_TEXT, make_font(13, QFont.Weight.Medium), 88),
        ]

        x = 6.0
        h = self.height()
        for idx, (txt, col, fnt, seg_w) in enumerate(segments):
            p.setFont(fnt)
            p.setPen(col)
            p.drawText(QRectF(x, 0, seg_w, h), int(Qt.AlignmentFlag.AlignCenter), txt)
            x += seg_w
            p.setPen(QPen(QColor(46, 51, 64), 1.0))
            p.drawLine(QPointF(x, 12.0), QPointF(x, h - 12.0))

        # 5-dot grip cluster on far right matching concept
        grip_cx = x + (self.width() - x) / 2.0
        grip_cy = h / 2.0
        p.setPen(Qt.PenStyle.NoPen)
        p.setBrush(COLOR_TEXT if self.unlocked else QColor(215, 220, 232))
        for dx, dy in [(0, 0), (-5.5, -5.5), (5.5, -5.5), (-5.5, 5.5), (5.5, 5.5)]:
            p.drawEllipse(QPointF(grip_cx + dx, grip_cy + dy), 1.7, 1.7)


class KeystrokePill(QWidget):
    """Draggable mini keystroke badge: [F] [M4] with live press highlight."""

    positionChanged = pyqtSignal(int, int)

    def __init__(self, cfg: ConfigStore, engine: MacroEngine, win32: Win32Bridge, parent: QWidget) -> None:
        super().__init__(parent)
        self.cfg = cfg
        self.engine = engine
        self.win32 = win32
        self.unlocked = False
        self._drag_offset: Optional[QPoint] = None
        self.setFixedSize(124, 40)

    def setUnlocked(self, unlocked: bool) -> None:
        self.unlocked = unlocked
        self.setCursor(Qt.CursorShape.SizeAllCursor if unlocked else Qt.CursorShape.ArrowCursor)
        self.update()

    def mousePressEvent(self, event) -> None:
        if self.unlocked and event.button() == Qt.MouseButton.LeftButton:
            self._drag_offset = event.globalPosition().toPoint() - self.pos()

    def mouseMoveEvent(self, event) -> None:
        if self.unlocked and self._drag_offset is not None:
            new_pos = event.globalPosition().toPoint() - self._drag_offset
            parent_rect = self.parentWidget().rect()
            nx = max(8, min(parent_rect.width() - self.width() - 8, new_pos.x()))
            ny = max(8, min(parent_rect.height() - self.height() - 8, new_pos.y()))
            self.move(nx, ny)
            self.positionChanged.emit(nx, ny)

    def mouseReleaseEvent(self, event) -> None:
        self._drag_offset = None

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        opacity = max(0.35, min(1.0, self.cfg.data.get("hud_opacity", 95) / 100.0))
        p.setOpacity(opacity)

        out_vk = int(self.cfg.data.get("output_vk", 0x46))
        trig_vk = int(self.cfg.data.get("trigger_vk", 0x05))
        out_lbl = f"[{vk_to_name(out_vk)}]"
        trig_lbl = f"[{vk_to_name(trig_vk)}]"

        trig_active = self.win32.is_vk_down(trig_vk) or self.engine.is_firing
        out_active = self.engine.is_firing or self.win32.is_vk_down(out_vk)

        rect = QRectF(0.5, 0.5, self.width() - 1.0, self.height() - 1.0)
        border_col = COLOR_CRIMSON if self.engine.is_firing else (
            QColor(82, 90, 110) if self.unlocked else COLOR_BORDER
        )
        p.setPen(QPen(border_col, 1.2))
        p.setBrush(QColor(15, 17, 22, 238))
        p.drawRoundedRect(rect, 7.0, 7.0)

        p.setFont(make_font(14, QFont.Weight.DemiBold))
        half_w = self.width() / 2.0
        p.setPen(COLOR_CRIMSON if out_active else COLOR_TEXT)
        p.drawText(QRectF(4, 0, half_w - 4, self.height()), int(Qt.AlignmentFlag.AlignCenter), out_lbl)

        p.setPen(COLOR_GREEN if trig_active else COLOR_TEXT)
        p.drawText(QRectF(half_w, 0, half_w - 4, self.height()), int(Qt.AlignmentFlag.AlignCenter), trig_lbl)


class RadialWheelWidget(QWidget):
    """4-Slice Minimalist Esports Radial Profile Selector (Low Ping, Clash Pro, Safe, Custom)."""

    SLICE_DEFS = [
        ("Low Ping", "pulse", 90.0),    # Top (Qt angle 90 deg)
        ("Clash Pro", "crown", 0.0),    # Right (Qt angle 0 deg)
        ("Safe", "shield", 270.0),      # Bottom (Qt angle 270 deg)
        ("Custom", "sliders", 180.0),   # Left (Qt angle 180 deg)
    ]

    profileClicked = pyqtSignal(str)

    def __init__(self, cfg: ConfigStore, parent: QWidget) -> None:
        super().__init__(parent)
        self.cfg = cfg
        self.hovered_profile: Optional[str] = None
        self.setFixedSize(292, 292)

    @staticmethod
    def _annular_sector(cx: float, cy: float, r_in: float, r_out: float, start_deg: float, span_deg: float) -> QPainterPath:
        out_rect = QRectF(cx - r_out, cy - r_out, 2 * r_out, 2 * r_out)
        in_rect = QRectF(cx - r_in, cy - r_in, 2 * r_in, 2 * r_in)

        start_rad = math.radians(start_deg)
        p0_x = cx + r_out * math.cos(start_rad)
        p0_y = cy - r_out * math.sin(start_rad)

        path = QPainterPath()
        path.moveTo(p0_x, p0_y)
        path.arcTo(out_rect, start_deg, span_deg)
        path.arcTo(in_rect, start_deg + span_deg, -span_deg)
        path.closeSubpath()
        return path

    def select_by_vector(self, dx: float, dy: float) -> Optional[str]:
        dist = math.hypot(dx, dy)
        if dist < 18.0:
            return self.hovered_profile
        deg = math.degrees(math.atan2(-dy, dx)) % 360.0
        if 45.0 <= deg < 135.0:
            sel = "Low Ping"
        elif deg >= 315.0 or deg < 45.0:
            sel = "Clash Pro"
        elif 225.0 <= deg < 315.0:
            sel = "Safe"
        else:
            sel = "Custom"
        if sel != self.hovered_profile:
            self.hovered_profile = sel
            self.update()
        return sel

    def mouseMoveEvent(self, event) -> None:
        c = QPointF(self.width() / 2.0, self.height() / 2.0)
        d = event.position() - c
        self.select_by_vector(d.x(), d.y())

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton:
            c = QPointF(self.width() / 2.0, self.height() / 2.0)
            d = event.position() - c
            chosen = self.select_by_vector(d.x(), d.y())
            if chosen:
                self.profileClicked.emit(chosen)

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        cx, cy = self.width() / 2.0, self.height() / 2.0
        r_out = 138.0
        r_in = 50.0
        gap = 3.6
        span = 90.0 - gap

        # Outer dark ring backdrop
        p.setPen(QPen(QColor(42, 46, 58, 220), 1.2))
        p.setBrush(QColor(14, 16, 21, 215))
        p.drawEllipse(QPointF(cx, cy), r_out + 4.0, r_out + 4.0)

        active = self.hovered_profile or self.cfg.data.get("active_profile", "Clash Pro")

        for name, icon_kind, center_deg in self.SLICE_DEFS:
            start_deg = center_deg - span / 2.0
            sector = self._annular_sector(cx, cy, r_in, r_out, start_deg, span)
            is_sel = (name == active)

            if is_sel:
                p.setPen(QPen(COLOR_CRIMSON, 1.6))
                p.setBrush(COLOR_CRIMSON_BG)
            else:
                p.setPen(QPen(QColor(54, 59, 73), 1.1))
                p.setBrush(QColor(24, 27, 34, 240))
            p.drawPath(sector)

            # Position icon + text label around mid-radius
            mid_r = (r_in + r_out) / 2.0
            rad = math.radians(center_deg)
            ix = cx + mid_r * math.cos(rad)
            iy = cy - mid_r * math.sin(rad)

            draw_vector_icon(p, icon_kind, QPointF(ix, iy - 10.0), 24.0, COLOR_TEXT)
            p.setFont(make_font(13, QFont.Weight.DemiBold if is_sel else QFont.Weight.Medium))
            p.setPen(COLOR_TEXT)
            p.drawText(
                QRectF(ix - 46.0, iy + 5.0, 92.0, 22.0),
                int(Qt.AlignmentFlag.AlignCenter),
                name,
            )

        # Center circle with diagonal Katana icon
        p.setPen(QPen(QColor(52, 57, 70), 1.3))
        p.setBrush(QColor(20, 22, 28, 250))
        p.drawEllipse(QPointF(cx, cy), r_in - 6.0, r_in - 6.0)
        draw_vector_icon(p, "sword", QPointF(cx, cy), 34.0, COLOR_TEXT)


# ============================================================================
# Main ByAzen Settings Window (5 Tabs: Macro, Clash, Widgets, Profiles, Binds)
# ============================================================================

class ByAzenMainWindow(QWidget):
    """Draggable compact minimalist esports settings panel matching the concept 1:1."""

    positionChanged = pyqtSignal(int, int)
    configUpdated = pyqtSignal()
    requestBindCapture = pyqtSignal(str)     # "trigger_vk" | "output_vk" | "menu_vk" | "radial_vk" | "panic_vk"
    requestResetPositions = pyqtSignal()
    requestCloseOverlay = pyqtSignal()
    requestQuitApp = pyqtSignal()

    def __init__(self, cfg: ConfigStore, parent: QWidget) -> None:
        super().__init__(parent)
        self.cfg = cfg
        self._drag_offset: Optional[QPoint] = None
        self._syncing = False
        self.capturing_target: Optional[str] = None
        self.setFixedSize(624, 440)
        self._build_ui()
        self.sync_from_config()

    def _build_ui(self) -> None:
        root = QVBoxLayout(self)
        root.setContentsMargins(0, 0, 0, 0)
        root.setSpacing(0)

        # ---- Header Bar (68 px) ----
        self.header = QWidget(self)
        self.header.setFixedHeight(68)
        h_lay = QHBoxLayout(self.header)
        h_lay.setContentsMargins(24, 14, 18, 12)
        h_lay.setSpacing(14)

        self.lbl_brand = QLabel("ByAzen")
        self.lbl_brand.setFont(make_font(26, QFont.Weight.Bold))
        self.lbl_brand.setStyleSheet("color: #F4F6FA; background: transparent;")
        h_lay.addWidget(self.lbl_brand)

        self.lbl_overlay_badge = QLabel("[END] OVERLAY")
        self.lbl_overlay_badge.setFont(make_font(12, QFont.Weight.DemiBold, mono=True))
        self.lbl_overlay_badge.setAlignment(Qt.AlignmentFlag.AlignCenter)
        self.lbl_overlay_badge.setFixedHeight(28)
        self.lbl_overlay_badge.setStyleSheet(
            "color: #E8ECF4; background-color: #171A21; "
            "border: 1px solid #323746; border-radius: 4px; padding: 0 10px;"
        )
        h_lay.addWidget(self.lbl_overlay_badge)
        h_lay.addStretch(1)

        self.btn_lock_hide = QPushButton("В игру [END]")
        self.btn_lock_hide.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_lock_hide.setFont(make_font(12, QFont.Weight.DemiBold))
        self.btn_lock_hide.setFixedHeight(28)
        self.btn_lock_hide.setStyleSheet(
            "QPushButton { color: #D4DAE6; background: #1B1F28; border: 1px solid #303544; "
            "border-radius: 5px; padding: 0 10px; }"
            "QPushButton:hover { background: #262B38; color: #FFFFFF; }"
        )
        self.btn_lock_hide.clicked.connect(self.requestCloseOverlay.emit)
        h_lay.addWidget(self.btn_lock_hide)

        self.btn_quit = QPushButton("×")
        self.btn_quit.setToolTip("Полностью закрыть ByAzen")
        self.btn_quit.setCursor(Qt.CursorShape.PointingHandCursor)
        self.btn_quit.setFont(make_font(17, QFont.Weight.Bold))
        self.btn_quit.setFixedSize(28, 28)
        self.btn_quit.setStyleSheet(
            "QPushButton { color: #9BA3B4; background: #181B22; border: 1px solid #2C313E; border-radius: 5px; }"
            "QPushButton:hover { color: #FFFFFF; background: #4A181E; border-color: #E53945; }"
        )
        self.btn_quit.clicked.connect(self.requestQuitApp.emit)
        h_lay.addWidget(self.btn_quit)

        root.addWidget(self.header)

        # ---- Body: Left Sidebar + Right Stacked Pages ----
        body = QWidget(self)
        b_lay = QHBoxLayout(body)
        b_lay.setContentsMargins(0, 0, 0, 0)
        b_lay.setSpacing(0)

        self.sidebar = QWidget(body)
        self.sidebar.setFixedWidth(168)
        s_lay = QVBoxLayout(self.sidebar)
        s_lay.setContentsMargins(12, 16, 12, 16)
        s_lay.setSpacing(8)

        self.tab_buttons: list[SidebarTabButton] = []
        tab_specs = [
            ("Macro", "sliders"),
            ("Clash", "target"),
            ("Widgets", "grid"),
            ("Profiles", "profile"),
            ("Binds", "keyboard"),
        ]
        for idx, (name, icon_k) in enumerate(tab_specs):
            btn = SidebarTabButton(name, icon_k, self.sidebar)
            btn.clicked.connect(lambda _=False, i=idx: self.switch_tab(i))
            s_lay.addWidget(btn)
            self.tab_buttons.append(btn)
        s_lay.addStretch(1)

        b_lay.addWidget(self.sidebar)

        self.pages = QStackedWidget(body)
        self.pages.addWidget(self._create_macro_page())
        self.pages.addWidget(self._create_clash_page())
        self.pages.addWidget(self._create_widgets_page())
        self.pages.addWidget(self._create_profiles_page())
        self.pages.addWidget(self._create_binds_page())
        b_lay.addWidget(self.pages, 1)

        root.addWidget(body, 1)
        self.switch_tab(0)

    @staticmethod
    def _make_toggle_row(title: str, checked: bool = False) -> Tuple[QWidget, QLabel, EsportsToggle]:
        row = QWidget()
        row.setFixedHeight(36)
        lay = QHBoxLayout(row)
        lay.setContentsMargins(2, 0, 2, 0)
        lbl = QLabel(title)
        lbl.setFont(make_font(14, QFont.Weight.DemiBold))
        lbl.setStyleSheet("color: #F4F6FA; background: transparent;")
        tog = EsportsToggle(checked)
        lay.addWidget(lbl)
        lay.addStretch(1)
        lay.addWidget(tog)
        return row, lbl, tog

    # ---- Page 0: Macro (Exact 1:1 layout from concept image) ----
    def _create_macro_page(self) -> QWidget:
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(24, 18, 26, 18)
        lay.setSpacing(14)

        r1, self.lbl_clash_macro, self.tog_macro = self._make_toggle_row("Clash Macro [M4]", True)
        self.tog_macro.toggled.connect(self._on_macro_toggle)
        lay.addWidget(r1)

        self.sld_cps = EsportsSlider("Target CPS", 5, 100, 48, "")
        self.sld_cps.valueChanged.connect(self._on_cps_slider)
        lay.addWidget(self.sld_cps)

        self.sld_delay = EsportsSlider("Delay", 3, 120, 18, " ms")
        self.sld_delay.valueChanged.connect(self._on_delay_slider)
        lay.addWidget(self.sld_delay)

        r4, _, self.tog_ping_sync = self._make_toggle_row("Ping Sync", True)
        self.tog_ping_sync.toggled.connect(lambda v: self._set_cfg("ping_sync", v))
        lay.addWidget(r4)

        r5, _, self.tog_no_alttab = self._make_toggle_row("No Alt-Tab Lock", True)
        self.tog_no_alttab.toggled.connect(lambda v: self._set_cfg("no_alttab_lock", v))
        lay.addWidget(r5)

        lay.addStretch(1)
        return page

    # ---- Page 1: Clash (Output F vs LMB, Hold vs Toggle, Hold Duration, Jitter, Safety) ----
    def _create_clash_page(self) -> QWidget:
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(24, 16, 26, 16)
        lay.setSpacing(10)

        lbl_out = QLabel("Что спамит макрос (быстрый выбор):")
        lbl_out.setFont(make_font(13, QFont.Weight.DemiBold))
        lbl_out.setStyleSheet("color: #C8CFDE; background: transparent;")
        lay.addWidget(lbl_out)

        self.seg_output = SegmentedChoice(
            [("F", "Клавиша [F]"), ("LMB", "Клик [LMB]"), ("CUSTOM", "Своя кнопка")],
            "F",
        )
        self.seg_output.changed.connect(self._on_quick_output_choice)
        lay.addWidget(self.seg_output)

        lbl_mode = QLabel("Режим активации кнопки:")
        lbl_mode.setFont(make_font(13, QFont.Weight.DemiBold))
        lbl_mode.setStyleSheet("color: #C8CFDE; background: transparent;")
        lay.addWidget(lbl_mode)

        self.seg_mode = SegmentedChoice(
            [("hold", "Удержание (Hold)"), ("toggle", "По нажатию (Toggle)")],
            "hold",
        )
        self.seg_mode.changed.connect(lambda m: self._set_cfg("activation_mode", m))
        lay.addWidget(self.seg_mode)

        self.sld_hold = EsportsSlider("Удержание клика", 1, 20, 3, " ms")
        self.sld_hold.valueChanged.connect(lambda v: self._set_cfg("hold_ms", v))
        lay.addWidget(self.sld_hold)

        self.sld_jitter = EsportsSlider("Рандомизация (Jitter)", 0, 20, 0, "%")
        self.sld_jitter.valueChanged.connect(lambda v: self._set_cfg("jitter_pct", v))
        lay.addWidget(self.sld_jitter)

        r_rbx, _, self.tog_only_roblox = self._make_toggle_row("Только в окне Roblox", True)
        self.tog_only_roblox.toggled.connect(lambda v: self._set_cfg("only_in_roblox", v))
        lay.addWidget(r_rbx)

        r_chat, _, self.tog_chat_safe = self._make_toggle_row("Пауза при открытии чата [/]", True)
        self.tog_chat_safe.toggled.connect(lambda v: self._set_cfg("chat_safe_pause", v))
        lay.addWidget(r_chat)

        lay.addStretch(1)
        return page

    # ---- Page 2: Widgets (HUD Visibility, Opacity, Reset Drag Positions) ----
    def _create_widgets_page(self) -> QWidget:
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(24, 18, 26, 18)
        lay.setSpacing(14)

        r_bar, _, self.tog_hud_bar = self._make_toggle_row("Верхний статус-бар (ByAzen HUD)", True)
        self.tog_hud_bar.toggled.connect(lambda v: self._set_cfg("show_hud_bar", v))
        lay.addWidget(r_bar)

        r_pill, _, self.tog_key_pill = self._make_toggle_row("Индикатор нажатий ([F] [M4])", True)
        self.tog_key_pill.toggled.connect(lambda v: self._set_cfg("show_key_pill", v))
        lay.addWidget(r_pill)

        self.sld_opacity = EsportsSlider("Прозрачность HUD", 40, 100, 95, "%")
        self.sld_opacity.valueChanged.connect(lambda v: self._set_cfg("hud_opacity", v))
        lay.addWidget(self.sld_opacity)

        hint = QLabel(
            "Пока открыто меню [END], ты можешь мышкой перетаскивать само окно ByAzen, "
            "верхний статус-бар и плашку [F] [M4] в любое место экрана."
        )
        hint.setWordWrap(True)
        hint.setFont(make_font(12, QFont.Weight.Normal))
        hint.setStyleSheet(
            "color: #9DA5B8; background: #161920; border: 1px solid #262B36; "
            "border-radius: 6px; padding: 10px;"
        )
        lay.addWidget(hint)

        btn_reset = QPushButton("Сбросить позиции виджетов по умолчанию")
        btn_reset.setCursor(Qt.CursorShape.PointingHandCursor)
        btn_reset.setFont(make_font(13, QFont.Weight.DemiBold))
        btn_reset.setFixedHeight(36)
        btn_reset.setStyleSheet(
            "QPushButton { color: #F4F6FA; background: #1E222C; border: 1px solid #323848; border-radius: 6px; }"
            "QPushButton:hover { background: #292F3D; border-color: #E53945; }"
        )
        btn_reset.clicked.connect(self.requestResetPositions.emit)
        lay.addWidget(btn_reset)

        lay.addStretch(1)
        return page

    # ---- Page 3: Profiles (4 Radial Presets + Save Current Settings) ----
    def _create_profiles_page(self) -> QWidget:
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(24, 18, 26, 18)
        lay.setSpacing(10)

        self.lbl_profiles_hint = QLabel("Профили колеса (удерживай [ALT] прямо в бою):")
        self.lbl_profiles_hint.setFont(make_font(13, QFont.Weight.DemiBold))
        self.lbl_profiles_hint.setStyleSheet("color: #C8CFDE; background: transparent;")
        lay.addWidget(self.lbl_profiles_hint)

        self.profile_btns: Dict[str, QPushButton] = {}
        descriptions = {
            "Low Ping": "56 CPS • 15 ms  (под низкий пинг)",
            "Clash Pro": "48 CPS • 18 ms  (темп для клинча)",
            "Safe": "26 CPS • 34 ms  (плавный с Jitter)",
            "Custom": "Твой настраиваемый пресет",
        }
        for prof_name, desc in descriptions.items():
            btn = QPushButton(f"{prof_name}   —   {desc}")
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setFont(make_font(13, QFont.Weight.DemiBold))
            btn.setFixedHeight(44)
            btn.clicked.connect(lambda _=False, p=prof_name: self.apply_profile(p))
            lay.addWidget(btn)
            self.profile_btns[prof_name] = btn

        btn_save_prof = QPushButton("Сохранить настройки в активный профиль")
        btn_save_prof.setCursor(Qt.CursorShape.PointingHandCursor)
        btn.setFont(make_font(13, QFont.Weight.DemiBold))
        btn_save_prof.setFont(make_font(13, QFont.Weight.DemiBold))
        btn_save_prof.setFixedHeight(36)
        btn_save_prof.setStyleSheet(
            "QPushButton { color: #F4F6FA; background: #1D212B; border: 1px solid #343A4A; border-radius: 6px; }"
            "QPushButton:hover { background: #44161B; border-color: #E53945; }"
        )
        btn_save_prof.clicked.connect(self._save_to_current_profile)
        lay.addWidget(btn_save_prof)

        lay.addStretch(1)
        return page

    # ---- Page 4: Binds (1-Click Rebinding for Trigger, Output, Menu, Radial, Panic) ----
    def _create_binds_page(self) -> QWidget:
        page = QWidget()
        lay = QVBoxLayout(page)
        lay.setContentsMargins(24, 16, 26, 14)
        lay.setSpacing(10)

        self.bind_buttons: Dict[str, QPushButton] = {}
        bind_rows = [
            ("trigger_vk", "Кнопка активации макроса"),
            ("output_vk", "Кнопка спама (F / LMB / любая)"),
            ("menu_vk", "Открыть / скрыть меню ByAzen"),
            ("radial_vk", "Колесо профилей (удержание)"),
            ("panic_vk", "Пауза макроса прямо в бою"),
        ]

        for field, title in bind_rows:
            row = QWidget()
            row.setFixedHeight(40)
            rl = QHBoxLayout(row)
            rl.setContentsMargins(0, 0, 0, 0)
            lbl = QLabel(title)
            lbl.setFont(make_font(13, QFont.Weight.Medium))
            lbl.setStyleSheet("color: #E8ECF4; background: transparent;")
            btn = QPushButton("[M4]")
            btn.setCursor(Qt.CursorShape.PointingHandCursor)
            btn.setFont(make_font(12, QFont.Weight.Bold, mono=True))
            btn.setFixedSize(116, 32)
            btn.clicked.connect(lambda _=False, f=field: self.requestBindCapture.emit(f))
            rl.addWidget(lbl)
            rl.addStretch(1)
            rl.addWidget(btn)
            lay.addWidget(row)
            self.bind_buttons[field] = btn

        # Instant helper buttons for Output = F or LMB
        quick_row = QWidget()
        ql = QHBoxLayout(quick_row)
        ql.setContentsMargins(0, 4, 0, 0)
        ql.setSpacing(8)
        for label, vk_val in [("Спам: [F]", 0x46), ("Спам: [LMB]", 0x01), ("Триггер: [M4]", 0x05)]:
            qb = QPushButton(label)
            qb.setCursor(Qt.CursorShape.PointingHandCursor)
            qb.setFont(make_font(12, QFont.Weight.DemiBold))
            qb.setFixedHeight(30)
            qb.setStyleSheet(
                "QPushButton { color: #CFD6E4; background: #191D26; border: 1px solid #2D3342; border-radius: 5px; }"
                "QPushButton:hover { color: #FFFFFF; border-color: #E53945; background: #2A161B; }"
            )
            if "Триггер" in label:
                qb.clicked.connect(lambda _=False, v=vk_val: self._quick_set_vk("trigger_vk", v))
            else:
                qb.clicked.connect(lambda _=False, v=vk_val: self._quick_set_vk("output_vk", v))
            ql.addWidget(qb)
        lay.addWidget(quick_row)

        self.lbl_bind_status = QLabel("Нажми на любую кнопку справа, чтобы назначить клавишу или кнопку мыши (ESC — отмена).")
        self.lbl_bind_status.setWordWrap(True)
        self.lbl_bind_status.setFont(make_font(11, QFont.Weight.Normal))
        self.lbl_bind_status.setStyleSheet("color: #8F97AA; background: transparent;")
        lay.addWidget(self.lbl_bind_status)

        lay.addStretch(1)
        return page

    # ---- Logic & Synchronization ----

    def switch_tab(self, index: int) -> None:
        self.pages.setCurrentIndex(index)
        for i, b in enumerate(self.tab_buttons):
            b.setActive(i == index)

    def _set_cfg(self, key: str, value) -> None:
        if self._syncing:
            return
        self.cfg.data[key] = value
        self.cfg.save()
        self.sync_from_config()
        self.configUpdated.emit()

    def _on_macro_toggle(self, val: bool) -> None:
        self._set_cfg("macro_enabled", val)

    def _on_cps_slider(self, cps: int) -> None:
        if self._syncing:
            return
        hold = int(self.cfg.data.get("hold_ms", 3))
        delay = max(3, int(round(1000.0 / max(1, cps))) - hold)
        self.cfg.data["target_cps"] = cps
        self.cfg.data["delay_ms"] = delay
        self.cfg.save()
        self.sync_from_config()
        self.configUpdated.emit()

    def _on_delay_slider(self, delay: int) -> None:
        if self._syncing:
            return
        hold = int(self.cfg.data.get("hold_ms", 3))
        cps = max(5, min(100, int(round(1000.0 / max(4, delay + hold)))))
        self.cfg.data["delay_ms"] = delay
        self.cfg.data["target_cps"] = cps
        self.cfg.save()
        self.sync_from_config()
        self.configUpdated.emit()

    def _on_quick_output_choice(self, choice: str) -> None:
        if choice == "F":
            self._quick_set_vk("output_vk", 0x46)
        elif choice == "LMB":
            self._quick_set_vk("output_vk", 0x01)
        else:
            self.switch_tab(4)
            self.requestBindCapture.emit("output_vk")

    def _quick_set_vk(self, field: str, vk: int) -> None:
        other = "output_vk" if field == "trigger_vk" else ("trigger_vk" if field == "output_vk" else "")
        if other and int(self.cfg.data.get(other, 0)) == vk:
            self.lbl_bind_status.setStyleSheet("color: #E53945; background: transparent;")
            self.lbl_bind_status.setText("Кнопка активации и кнопка спама должны отличаться (например M4 → F)!")
            return
        self.cfg.data[field] = vk
        self.cfg.save()
        self.sync_from_config()
        self.configUpdated.emit()

    def apply_profile(self, name: str) -> None:
        prof = self.cfg.data.get("profiles", {}).get(name)
        if not prof:
            return
        self.cfg.data["active_profile"] = name
        for k in ("target_cps", "delay_ms", "hold_ms", "jitter_pct", "ping_sync"):
            if k in prof:
                self.cfg.data[k] = prof[k]
        self.cfg.save()
        self.sync_from_config()
        self.configUpdated.emit()

    def _save_to_current_profile(self) -> None:
        cur = self.cfg.data.get("active_profile", "Custom")
        self.cfg.data.setdefault("profiles", {})[cur] = {
            "target_cps": int(self.cfg.data.get("target_cps", 48)),
            "delay_ms": int(self.cfg.data.get("delay_ms", 18)),
            "hold_ms": int(self.cfg.data.get("hold_ms", 3)),
            "jitter_pct": int(self.cfg.data.get("jitter_pct", 0)),
            "ping_sync": bool(self.cfg.data.get("ping_sync", True)),
        }
        self.cfg.save()
        self.sync_from_config()

    def sync_from_config(self) -> None:
        self._syncing = True
        d = self.cfg.data
        trig_name = vk_to_name(int(d.get("trigger_vk", 0x05)))
        menu_name = vk_to_name(int(d.get("menu_vk", 0x23)))
        radial_name = vk_to_name(int(d.get("radial_vk", 0x12)))
        out_vk = int(d.get("output_vk", 0x46))

        self.lbl_clash_macro.setText(f"Clash Macro [{trig_name}]")
        self.lbl_overlay_badge.setText(f"[{menu_name}] OVERLAY")
        self.btn_lock_hide.setText(f"В игру [{menu_name}]")
        self.lbl_profiles_hint.setText(f"Профили колеса (удерживай [{radial_name}] прямо в бою):")

        self.tog_macro.setChecked(bool(d.get("macro_enabled", True)))
        self.sld_cps.setValue(int(d.get("target_cps", 48)))
        self.sld_delay.setValue(int(d.get("delay_ms", 18)))
        self.tog_ping_sync.setChecked(bool(d.get("ping_sync", True)))
        self.tog_no_alttab.setChecked(bool(d.get("no_alttab_lock", True)))

        if out_vk == 0x46:
            self.seg_output.setCurrent("F")
        elif out_vk == 0x01:
            self.seg_output.setCurrent("LMB")
        else:
            self.seg_output.setCurrent("CUSTOM")

        self.seg_mode.setCurrent(str(d.get("activation_mode", "hold")))
        self.sld_hold.setValue(int(d.get("hold_ms", 3)))
        self.sld_jitter.setValue(int(d.get("jitter_pct", 0)))
        self.tog_only_roblox.setChecked(bool(d.get("only_in_roblox", True)))
        self.tog_chat_safe.setChecked(bool(d.get("chat_safe_pause", True)))

        self.tog_hud_bar.setChecked(bool(d.get("show_hud_bar", True)))
        self.tog_key_pill.setChecked(bool(d.get("show_key_pill", True)))
        self.sld_opacity.setValue(int(d.get("hud_opacity", 95)))

        active_prof = d.get("active_profile", "Clash Pro")
        for pname, pbtn in self.profile_btns.items():
            if pname == active_prof:
                pbtn.setStyleSheet(
                    "QPushButton { text-align: left; padding-left: 14px; color: #FFFFFF; "
                    "background: #44161B; border: 1.5px solid #E53945; border-radius: 6px; }"
                )
            else:
                pbtn.setStyleSheet(
                    "QPushButton { text-align: left; padding-left: 14px; color: #D0D6E2; "
                    "background: #181C24; border: 1px solid #2B303D; border-radius: 6px; }"
                    "QPushButton:hover { background: #212632; color: #FFFFFF; }"
                )

        for field, bbtn in self.bind_buttons.items():
            if self.capturing_target == field:
                bbtn.setText("[ Нажми... ]")
                bbtn.setStyleSheet(
                    "QPushButton { color: #FFFFFF; background: #44161B; "
                    "border: 1.5px solid #E53945; border-radius: 5px; }"
                )
            else:
                v = int(d.get(field, 0))
                bbtn.setText(f"[ {vk_to_name(v)} ]")
                bbtn.setStyleSheet(
                    "QPushButton { color: #F4F6FA; background: #191D26; "
                    "border: 1px solid #323848; border-radius: 5px; }"
                    "QPushButton:hover { border-color: #E53945; background: #232834; }"
                )

        self._syncing = False

    # ---- Draggable Header ----

    def mousePressEvent(self, event) -> None:
        if event.button() == Qt.MouseButton.LeftButton and event.position().y() <= 68:
            self._drag_offset = event.globalPosition().toPoint() - self.pos()

    def mouseMoveEvent(self, event) -> None:
        if self._drag_offset is not None:
            new_pos = event.globalPosition().toPoint() - self._drag_offset
            parent_rect = self.parentWidget().rect()
            nx = max(8, min(parent_rect.width() - self.width() - 8, new_pos.x()))
            ny = max(8, min(parent_rect.height() - self.height() - 8, new_pos.y()))
            self.move(nx, ny)
            self.positionChanged.emit(nx, ny)

    def mouseReleaseEvent(self, event) -> None:
        self._drag_offset = None

    def paintEvent(self, event) -> None:
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)

        rect = QRectF(0.5, 0.5, self.width() - 1.0, self.height() - 1.0)
        p.setPen(QPen(COLOR_BORDER, 1.3))
        p.setBrush(COLOR_BG_MAIN)
        p.drawRoundedRect(rect, 9.0, 9.0)

        # Header horizontal divider & Sidebar vertical divider
        p.setPen(QPen(COLOR_DIVIDER, 1.0))
        p.drawLine(QPointF(1.0, 68.0), QPointF(self.width() - 1.0, 68.0))
        p.drawLine(QPointF(168.0, 68.0), QPointF(168.0, self.height() - 1.0))


# ============================================================================
# Unified Fullscreen Transparent Overlay Controller
# ============================================================================

class ByAzenOverlay(QWidget):
    """Fullscreen transparent TopMost overlay hosting the Menu, HUD Widgets, and Radial Wheel."""

    def __init__(self, cfg: ConfigStore, win32: Win32Bridge, engine: MacroEngine) -> None:
        super().__init__()
        self.cfg = cfg
        self.win32 = win32
        self.engine = engine

        self.menu_open = True
        self.radial_open = False
        self._radial_origin: Tuple[int, int] = (0, 0)
        self._prev_menu_vk = False
        self._prev_panic_vk = False
        self._prev_radial_vk = False
        self._capture_field: Optional[str] = None
        self._capture_ready_at: float = 0.0

        self.setWindowTitle("ByAzen Overlay")
        self.setWindowFlags(
            Qt.WindowType.FramelessWindowHint
            | Qt.WindowType.WindowStaysOnTopHint
            | Qt.WindowType.Tool
        )
        self.setAttribute(Qt.WidgetAttribute.WA_TranslucentBackground, True)

        screen = QApplication.primaryScreen()
        geom = screen.geometry() if screen else QRect(0, 0, 1920, 1080)
        self.setGeometry(geom)

        refresh = int(round(screen.refreshRate())) if screen and screen.refreshRate() > 30 else 240

        # Child widgets
        self.hud_bar = HudStatusBar(cfg, engine, self)
        self.hud_bar.monitor_fps = refresh
        self.hud_bar.positionChanged.connect(lambda x, y: self._save_pos("hud_bar", x, y))

        self.key_pill = KeystrokePill(cfg, engine, win32, self)
        self.key_pill.positionChanged.connect(lambda x, y: self._save_pos("key_pill", x, y))

        self.radial = RadialWheelWidget(cfg, self)
        self.radial.profileClicked.connect(self._on_radial_clicked)
        self.radial.hide()

        self.main_win = ByAzenMainWindow(cfg, self)
        self.main_win.positionChanged.connect(lambda x, y: self._save_pos("menu", x, y))
        self.main_win.configUpdated.connect(self._apply_visibility)
        self.main_win.requestBindCapture.connect(self._start_bind_capture)
        self.main_win.requestResetPositions.connect(self.reset_positions)
        self.main_win.requestCloseOverlay.connect(lambda: self.set_menu_open(False))
        self.main_win.requestQuitApp.connect(QApplication.instance().quit)

        self._restore_positions()
        self._apply_visibility()
        self._setup_tray()

        # 120 Hz UI + Hotkey Polling Timer
        self._timer = QTimer(self)
        self._timer.setInterval(12)
        self._timer.timeout.connect(self._tick)
        self._timer.start()

    def _make_app_icon(self) -> QIcon:
        pix = QPixmap(64, 64)
        pix.fill(Qt.GlobalColor.transparent)
        p = QPainter(pix)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        p.setPen(QPen(COLOR_CRIMSON, 3.0))
        p.setBrush(QColor(17, 19, 24))
        p.drawRoundedRect(QRectF(4, 4, 56, 56), 14, 14)
        draw_vector_icon(p, "sword", QPointF(32, 32), 36.0, COLOR_TEXT)
        p.end()
        return QIcon(pix)

    def _setup_tray(self) -> None:
        icon = self._make_app_icon()
        self.setWindowIcon(icon)
        if not QSystemTrayIcon.isSystemTrayAvailable():
            self.tray = None
            return
        self.tray = QSystemTrayIcon(icon, self)
        self.tray.setToolTip("ByAzen — Roblox Blade Ball Overlay")

        from PyQt6.QtWidgets import QMenu
        menu = QMenu()
        act_toggle = QAction("Открыть / Скрыть меню [END]", self)
        act_toggle.triggered.connect(lambda: self.set_menu_open(not self.menu_open))
        menu.addAction(act_toggle)

        act_macro = QAction("Вкл / Выкл макрос", self)
        act_macro.triggered.connect(self._toggle_macro_hotkey)
        menu.addAction(act_macro)

        act_reset = QAction("Сбросить позиции виджетов", self)
        act_reset.triggered.connect(self.reset_positions)
        menu.addAction(act_reset)

        menu.addSeparator()
        act_quit = QAction("Выход из ByAzen", self)
        act_quit.triggered.connect(QApplication.instance().quit)
        menu.addAction(act_quit)

        self.tray.setContextMenu(menu)
        self.tray.activated.connect(
            lambda reason: self.set_menu_open(not self.menu_open)
            if reason == QSystemTrayIcon.ActivationReason.Trigger else None
        )
        self.tray.show()

    def _restore_positions(self) -> None:
        w, h = self.width(), self.height()
        pos = self.cfg.data.get("positions", {})

        mx, my = pos.get("menu", [36, 155])
        self.main_win.move(max(8, min(w - self.main_win.width() - 8, mx)), max(8, min(h - self.main_win.height() - 8, my)))

        hx, hy = pos.get("hud_bar", [-1, 36])
        if hx < 0:
            hx = w - self.hud_bar.width() - 28
        self.hud_bar.move(max(8, min(w - self.hud_bar.width() - 8, hx)), max(8, min(h - self.hud_bar.height() - 8, hy)))

        kx, ky = pos.get("key_pill", [-1, -1])
        if kx < 0 or ky < 0:
            kx = w - 260
            ky = int(h * 0.74)
        self.key_pill.move(max(8, min(w - self.key_pill.width() - 8, kx)), max(8, min(h - self.key_pill.height() - 8, ky)))

        rx, ry = pos.get("radial", [-1, -1])
        if rx < 0 or ry < 0:
            rx = w - self.radial.width() - 56
            ry = int(h * 0.50 - self.radial.height() / 2)
        self.radial.move(max(8, min(w - self.radial.width() - 8, rx)), max(8, min(h - self.radial.height() - 8, ry)))

    def reset_positions(self) -> None:
        self.cfg.data["positions"] = {
            "menu": [36, 155],
            "hud_bar": [-1, 36],
            "key_pill": [-1, -1],
            "radial": [-1, -1],
        }
        self.cfg.save()
        self._restore_positions()

    def _save_pos(self, name: str, x: int, y: int) -> None:
        self.cfg.data.setdefault("positions", {})[name] = [x, y]
        self.cfg.save()

    def _apply_visibility(self) -> None:
        self.hud_bar.setVisible(bool(self.cfg.data.get("show_hud_bar", True)))
        self.key_pill.setVisible(bool(self.cfg.data.get("show_key_pill", True)))
        self.hud_bar.setUnlocked(self.menu_open)
        self.key_pill.setUnlocked(self.menu_open)
        self.main_win.setVisible(self.menu_open)
        self.update()

    def set_menu_open(self, open_state: bool) -> None:
        self.menu_open = open_state
        if not open_state:
            self._cancel_bind_capture()
        self._apply_visibility()
        hwnd = int(self.winId())
        if open_state:
            self.win32.get_foreground_title()  # remember Roblox HWND if active
            self.win32.apply_overlay_style(hwnd, click_through=False)
            self.raise_()
            self.activateWindow()
        else:
            self.win32.apply_overlay_style(hwnd, click_through=True)
            if self.cfg.data.get("no_alttab_lock", True):
                self.win32.refocus_roblox()

    def showEvent(self, event) -> None:
        super().showEvent(event)
        hwnd = int(self.winId())
        self.win32.apply_overlay_style(hwnd, click_through=not self.menu_open)

    def _toggle_macro_hotkey(self) -> None:
        cur = bool(self.cfg.data.get("macro_enabled", True))
        self.cfg.data["macro_enabled"] = not cur
        self.cfg.save()
        self.main_win.sync_from_config()
        self.hud_bar.update()

    def _on_radial_clicked(self, prof_name: str) -> None:
        self.main_win.apply_profile(prof_name)
        self.radial.hide()
        self.radial_open = False

    # ---- Interactive Key / Mouse Bind Capture ----

    def _start_bind_capture(self, field: str) -> None:
        self._capture_field = field
        # 220ms grace period so initial mouse click on button isn't captured immediately
        self._capture_ready_at = time.perf_counter() + 0.22
        self.main_win.capturing_target = field
        self.main_win.lbl_bind_status.setStyleSheet("color: #3ED470; background: transparent;")
        self.main_win.lbl_bind_status.setText("Жду нажатия любой клавиши или кнопки мыши (M4, M5, F, LMB...) • ESC для отмены")
        self.main_win.sync_from_config()

    def _cancel_bind_capture(self) -> None:
        self._capture_field = None
        self.main_win.capturing_target = None
        self.main_win.lbl_bind_status.setStyleSheet("color: #8F97AA; background: transparent;")
        self.main_win.lbl_bind_status.setText(
            "Нажми на любую кнопку справа, чтобы назначить клавишу или кнопку мыши (ESC — отмена)."
        )
        self.main_win.sync_from_config()

    def _poll_bind_capture(self) -> bool:
        if not self._capture_field or not IS_WINDOWS:
            return False
        if time.perf_counter() < self._capture_ready_at:
            return True

        if self.win32.is_vk_down(0x1B):  # ESC cancels
            self._cancel_bind_capture()
            return True

        for vk in CAPTURE_CANDIDATE_VKS:
            if self.win32.is_vk_down(vk):
                field = self._capture_field
                # Disallow menu_vk = LMB so user doesn't trap left click
                if field == "menu_vk" and vk == 0x01:
                    continue
                other = "output_vk" if field == "trigger_vk" else ("trigger_vk" if field == "output_vk" else "")
                if other and int(self.cfg.data.get(other, 0)) == vk:
                    self.main_win.lbl_bind_status.setStyleSheet("color: #E53945; background: transparent;")
                    self.main_win.lbl_bind_status.setText(
                        f"Кнопка [{vk_to_name(vk)}] уже занята парным биндом! Выбери другую клавишу."
                    )
                    self._capture_field = None
                    self.main_win.capturing_target = None
                    self.main_win.sync_from_config()
                    return True

                self.cfg.data[field] = vk
                self.cfg.save()
                self._capture_field = None
                self.main_win.capturing_target = None
                self.main_win.lbl_bind_status.setStyleSheet("color: #3ED470; background: transparent;")
                self.main_win.lbl_bind_status.setText(f"Успешно назначено: [{vk_to_name(vk)}]")
                self.main_win.sync_from_config()
                self._prev_menu_vk = True
                return True
        return True

    # ---- Global Tick: Hotkeys, Radial Wheel, Live HUD Refresh ----

    def _tick(self) -> None:
        self.win32.get_foreground_title()

        if self._poll_bind_capture():
            self.hud_bar.update()
            self.key_pill.update()
            return

        if IS_WINDOWS:
            # 1. Menu Toggle Hotkey ([END] by default)
            menu_vk = int(self.cfg.data.get("menu_vk", 0x23))
            m_down = self.win32.is_vk_down(menu_vk)
            if m_down and not self._prev_menu_vk:
                self.set_menu_open(not self.menu_open)
            self._prev_menu_vk = m_down

            # 2. Panic Macro Toggle Hotkey ([F10] by default)
            panic_vk = int(self.cfg.data.get("panic_vk", 0x79))
            p_down = self.win32.is_vk_down(panic_vk)
            if p_down and not self._prev_panic_vk:
                self._toggle_macro_hotkey()
            self._prev_panic_vk = p_down

            # 3. Radial Wheel Hold Hotkey ([ALT] by default — works click-through without stealing Roblox focus!)
            radial_vk = int(self.cfg.data.get("radial_vk", 0x12))
            r_down = self.win32.is_vk_down(radial_vk) and not self.win32.is_vk_down(0x09)  # ignore Alt+Tab
            if r_down and not self._prev_radial_vk:
                self.radial_open = True
                self._radial_origin = self.win32.get_cursor_pos()
                self.radial.hovered_profile = self.cfg.data.get("active_profile", "Clash Pro")
                self.radial.show()
                self.radial.update()
            elif r_down and self.radial_open:
                cx, cy = self.win32.get_cursor_pos()
                dx = cx - self._radial_origin[0]
                dy = cy - self._radial_origin[1]
                self.radial.select_by_vector(dx, dy)
                # Also allow quick number keys 1-4 while holding radial key (Shift-Lock friendly!)
                for num_vk, prof in [(0x31, "Low Ping"), (0x32, "Clash Pro"), (0x33, "Safe"), (0x34, "Custom")]:
                    if self.win32.is_vk_down(num_vk):
                        self.radial.hovered_profile = prof
                        self.radial.update()
            elif not r_down and self._prev_radial_vk and self.radial_open:
                chosen = self.radial.hovered_profile
                self.radial.hide()
                self.radial_open = False
                if chosen and chosen != self.cfg.data.get("active_profile"):
                    self.main_win.apply_profile(chosen)
            self._prev_radial_vk = r_down

        self.hud_bar.update()
        self.key_pill.update()

    def mousePressEvent(self, event) -> None:
        # Clicking empty space outside all panels while No Alt-Tab Lock is ON returns directly to Roblox
        if self.menu_open and self.cfg.data.get("no_alttab_lock", True):
            pt = event.position().toPoint()
            if (
                not self.main_win.geometry().contains(pt)
                and not self.hud_bar.geometry().contains(pt)
                and not self.key_pill.geometry().contains(pt)
            ):
                self.set_menu_open(False)

    def keyPressEvent(self, event) -> None:
        # Fallback keyboard handling when window itself has focus (also works on non-Windows preview)
        if self._capture_field and not IS_WINDOWS:
            if event.key() == Qt.Key.Key_Escape:
                self._cancel_bind_capture()
            return
        if event.key() == Qt.Key.Key_End and not IS_WINDOWS:
            self.set_menu_open(not self.menu_open)
        elif event.key() == Qt.Key.Key_Q and (event.modifiers() & Qt.KeyboardModifier.ControlModifier):
            QApplication.instance().quit()

    def paintEvent(self, event) -> None:
        if not self.menu_open:
            return
        # Subtle bottom-center hint pill when menu is unlocked so player knows overlay is in config mode
        p = QPainter(self)
        p.setRenderHint(QPainter.RenderHint.Antialiasing)
        menu_key = vk_to_name(int(self.cfg.data.get("menu_vk", 0x23)))
        txt = f"Режим настройки ByAzen • Перетаскивай виджеты мышкой • [{menu_key}] или клик мимо — в игру"
        fnt = make_font(12, QFont.Weight.Medium)
        p.setFont(fnt)
        bw = p.fontMetrics().horizontalAdvance(txt) + 38.0
        banner = QRectF((self.width() - bw) / 2.0, self.height() - 46.0, bw, 30.0)
        p.setPen(QPen(QColor(55, 61, 76), 1.0))
        p.setBrush(QColor(15, 17, 22, 228))
        p.drawRoundedRect(banner, 6.0, 6.0)
        p.setPen(QColor(210, 216, 228))
        p.drawText(banner, int(Qt.AlignmentFlag.AlignCenter), txt)


# ============================================================================
# Entrypoint
# ============================================================================

def main() -> int:
    Win32Bridge.enable_dpi_awareness()
    app = QApplication(sys.argv)
    app.setQuitOnLastWindowClosed(False)

    cfg = ConfigStore()
    win32 = Win32Bridge()
    engine = MacroEngine(cfg, win32)
    engine.start()

    overlay = ByAzenOverlay(cfg, win32, engine)
    overlay.show()

    def _on_exit() -> None:
        engine.stop()
        win32.cleanup()

    app.aboutToQuit.connect(_on_exit)
    return app.exec()


if __name__ == "__main__":
    sys.exit(main())
