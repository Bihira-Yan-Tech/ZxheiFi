"""
theme.py - ZxheiFi Setup Companion color theme.

Same palette as mikrotik/gui/style.css's :root tokens (sampled directly
from the ZxheiFi logo) - kept in sync by hand since this is a separate
Python/CustomTkinter app, not a shared CSS file.
"""
import customtkinter as ctk

INK = "#081527"
PANEL = "#0d1e33"
PANEL_INSET = "#050d18"
ACCENT = "#2dd4c9"
ACCENT_DARK = "#1c7fc4"
PESO = "#34d399"
ALERT = "#ef4b45"
TEXT = "#eef6fa"
TEXT_DIM = "#7d93a3"
BORDER = "#22384a"


def apply():
    ctk.set_appearance_mode("dark")
    ctk.set_default_color_theme("dark-blue")


BUTTON_KW = dict(fg_color=ACCENT, hover_color=ACCENT_DARK, text_color="#04141a")
SECONDARY_BUTTON_KW = dict(fg_color=PANEL_INSET, hover_color=BORDER, text_color=TEXT,
                            border_width=1, border_color=BORDER)
DANGER_BUTTON_KW = dict(fg_color=ALERT, hover_color="#c73f39", text_color=TEXT)
