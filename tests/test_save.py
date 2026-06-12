"""Round-trip save tests: load(src) -> save(dst) -> assert structural & byte fidelity."""
import hashlib

import psdparse


def _sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def _read(path) -> bytes:
    with open(path, "rb") as f:
        return f.read()


def test_save_byte_identical_ui(tmp_path, sample_ui_psd):
    dst = tmp_path / "ui_rt.psd"
    p = psdparse.PSDFile()
    assert p.load(str(sample_ui_psd))
    assert p.save(str(dst))
    assert _read(sample_ui_psd) == _read(dst)


def test_save_byte_identical_large(tmp_path, sample_large_psd):
    dst = tmp_path / "large_rt.psd"
    p = psdparse.PSDFile()
    assert p.load(str(sample_large_psd))
    assert p.save(str(dst))
    assert _read(sample_large_psd) == _read(dst)


def test_save_then_reload_preserves_layers(tmp_path, sample_ui_psd):
    dst = tmp_path / "ui_rt.psd"
    p1 = psdparse.PSDFile()
    assert p1.load(str(sample_ui_psd))
    assert p1.save(str(dst))
    p2 = psdparse.PSDFile()
    assert p2.load(str(dst))
    assert p1.header.width == p2.header.width
    assert p1.header.height == p2.header.height
    assert p1.header.channels == p2.header.channels
    assert p1.header.depth == p2.header.depth
    assert p1.header.mode == p2.header.mode
    assert len(p1.layers) == len(p2.layers)
    for l1, l2 in zip(p1.layers, p2.layers):
        assert l1.top == l2.top and l1.left == l2.left
        assert l1.bottom == l2.bottom and l1.right == l2.right
        assert l1.blend_mode_key == l2.blend_mode_key
        assert l1.opacity == l2.opacity
        assert l1.layer_type == l2.layer_type
        assert l1.name_unicode == l2.name_unicode


def test_save_then_reload_merged_image_matches(tmp_path, sample_large_psd):
    dst = tmp_path / "large_rt.psd"
    p1 = psdparse.PSDFile()
    assert p1.load(str(sample_large_psd))
    assert p1.save(str(dst))
    p2 = psdparse.PSDFile()
    assert p2.load(str(dst))
    assert _sha(p1.merged_image()) == _sha(p2.merged_image())


def test_save_then_reload_layer_pixels_match(tmp_path, sample_large_psd):
    """All NORMAL layers' pixels survive a round-trip identically."""
    dst = tmp_path / "large_rt.psd"
    p1 = psdparse.PSDFile()
    assert p1.load(str(sample_large_psd))
    assert p1.save(str(dst))
    p2 = psdparse.PSDFile()
    assert p2.load(str(dst))
    checked = 0
    for i, l in enumerate(p1.layers):
        if l.layer_type != psdparse.LayerType.NORMAL: continue
        if l.width <= 0 or l.height <= 0: continue
        assert _sha(p1.layer_image(i)) == _sha(p2.layer_image(i)), f"layer {i} pixel mismatch"
        checked += 1
    assert checked > 0


def test_save_fails_when_unloaded(tmp_path):
    p = psdparse.PSDFile()
    assert not p.save(str(tmp_path / "nope.psd"))
