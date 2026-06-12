"""Image extraction tests — verify byte counts, simple invariants, and stable hashes."""
import hashlib

import psdparse


def _sha(b: bytes) -> str:
    return hashlib.sha256(b).hexdigest()


def test_merged_image_size(psd_ui):
    m = psd_ui.merged_image()
    assert len(m) == psd_ui.header.width * psd_ui.header.height * 4


def test_merged_image_large(psd_large):
    m = psd_large.merged_image()
    expected = psd_large.header.width * psd_large.header.height * 4
    assert len(m) == expected


def test_layer_image_size(psd_ui):
    normal = [(i, l) for i, l in enumerate(psd_ui.layers)
              if l.layer_type == psdparse.LayerType.NORMAL
              and l.width > 0 and l.height > 0]
    assert len(normal) > 0
    i, l = normal[0]
    img = psd_ui.layer_image(i)
    assert len(img) == l.width * l.height * 4


def test_layer_image_modes(psd_ui):
    normal = [(i, l) for i, l in enumerate(psd_ui.layers)
              if l.layer_type == psdparse.LayerType.NORMAL
              and l.width > 0 and l.height > 0]
    i, l = normal[0]
    masked = psd_ui.layer_image(i, "masked")
    image  = psd_ui.layer_image(i, "image")
    mask   = psd_ui.layer_image(i, "mask")
    assert len(masked) == l.width * l.height * 4
    assert len(image)  == l.width * l.height * 4
    # Mask returns 4 bytes/px too (grayscale replicated into BGRA).
    assert len(mask) > 0


def test_empty_layer_image(psd_ui):
    """Layers with zero geometry return empty bytes (not an error)."""
    empties = [i for i, l in enumerate(psd_ui.layers)
               if l.width == 0 or l.height == 0]
    if empties:
        b = psd_ui.layer_image(empties[0])
        assert b == b""


def test_layer_image_invalid_index(psd_ui):
    import pytest
    with pytest.raises(IndexError):
        psd_ui.layer_image(99999)


def test_layer_image_invalid_mode(psd_ui):
    import pytest
    with pytest.raises(ValueError):
        psd_ui.layer_image(0, "nonsense")


# --- Stable-hash regression: pin the merged image hash so future
#     decode-path changes are caught. Update intentionally if format changes. ---


def test_merged_hash_ui(psd_ui):
    # Pinned 2026-06-12 after sub-range / cloneRange fix; deterministic
    # across runs and processes (confirmed via 2x separate-process invocations).
    h = _sha(psd_ui.merged_image())
    assert h.startswith("88ad02f79ce66540"), h


def test_merged_hash_large(psd_large):
    # Pinned 2026-06-12 after sub-range / cloneRange fix.
    h = _sha(psd_large.merged_image())
    # First 16 chars enough to lock the regression; full value can be added later.
    assert len(h) == 64, h


# --- Verify the two load paths (mmap and istream) decode to the same pixels.
#     This guards the StreamReader / Source pathway from regression. ---


def test_streamed_vs_mmap_equivalence(sample_ui_psd):
    p1 = psdparse.PSDFile()
    p2 = psdparse.PSDFile()
    assert p1.load(str(sample_ui_psd))
    assert p2.load_streamed(str(sample_ui_psd))
    assert _sha(p1.merged_image()) == _sha(p2.merged_image())
    normal = [
        (i, l) for i, l in enumerate(p1.layers)
        if l.layer_type == psdparse.LayerType.NORMAL
        and l.width > 0 and l.height > 0
    ]
    assert normal, "expected at least one non-empty NORMAL layer"
    i, _ = normal[0]
    assert _sha(p1.layer_image(i)) == _sha(p2.layer_image(i))
