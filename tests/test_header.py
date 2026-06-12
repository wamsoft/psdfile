"""Header & basic-attribute regression tests."""
import psdparse


def test_load_ui(psd_ui):
    assert psd_ui.is_loaded
    assert psd_ui.header.width  == 800
    assert psd_ui.header.height == 600
    assert psd_ui.header.channels == 3
    assert psd_ui.header.mode == psdparse.COLOR_MODE_RGB
    assert psd_ui.header.depth == 8


def test_load_large(psd_large):
    assert psd_large.is_loaded
    assert psd_large.header.width  == 2500
    assert psd_large.header.height == 3500
    assert psd_large.header.channels >= 3


def test_unloaded_state():
    p = psdparse.PSDFile()
    assert not p.is_loaded
    assert len(p.layers) == 0


def test_load_nonexistent():
    p = psdparse.PSDFile()
    assert not p.load(r"D:\does\not\exist.psd")
    assert not p.is_loaded
