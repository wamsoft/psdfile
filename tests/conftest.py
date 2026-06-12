"""pytest fixtures for psdparse tests.

Injects the built pybind11 module into sys.path and exposes the sample PSDs
in tests/data/ as fixtures.
"""
import os
import sys
from pathlib import Path

import pytest

REPO = Path(__file__).resolve().parent.parent

# Locate the most recent build of the psdparse Python module.
_CANDIDATES = [
    REPO / "build" / "x64-windows-python" / "python" / "Release",
    REPO / "build" / "x64-windows-python" / "python" / "Debug",
]
for _d in _CANDIDATES:
    if _d.is_dir() and any(_d.glob("psdparse*.pyd")):
        sys.path.insert(0, str(_d))
        break

# Allow override via env var (CI / custom build dirs).
_env_dir = os.environ.get("PSDPARSE_BUILD_DIR")
if _env_dir:
    sys.path.insert(0, _env_dir)

import psdparse  # noqa: E402  -- after sys.path setup


DATA = REPO / "tests" / "data"


def _find_sample(*names):
    """Return the first existing path among given basenames in tests/data/
    or the repo root (legacy location)."""
    for name in names:
        for root in (DATA, REPO):
            p = root / name
            if p.is_file():
                return p
    pytest.skip(f"sample PSD not found: {names!r}")


@pytest.fixture(scope="session")
def sample_ui_psd():
    return _find_sample("UI-PSDサンプル.psd", "UI-PSD-sample.psd")


@pytest.fixture(scope="session")
def sample_large_psd():
    return _find_sample("園部由夏_a.psd", "large.psd")


@pytest.fixture
def psd_ui(sample_ui_psd):
    p = psdparse.PSDFile()
    assert p.load(str(sample_ui_psd))
    return p


@pytest.fixture
def psd_large(sample_large_psd):
    p = psdparse.PSDFile()
    assert p.load(str(sample_large_psd))
    return p
