"""Export a PSD's structure to JSON and every layer image to PNG.

Usage:
    python tools/psd_export.py <input.psd> [--out-dir <dir>] [--mode masked|image|mask]

Output layout (under --out-dir, default: <psd-stem>_export/):
    layers.json           -- all header + layer metadata
    merged.png            -- composite image
    layers/000_<name>.png -- per-layer image (index zero-padded, sanitized name)

Requires: psdparse module (built via PRESET=x64-windows-python), Pillow.
"""
import argparse
import json
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
for sub in ("Release", "Debug"):
    d = REPO / "build" / "x64-windows-python" / "python" / sub
    if d.is_dir() and any(d.glob("psdparse*.pyd")):
        sys.path.insert(0, str(d))
        break

import psdparse
from PIL import Image


_SAFE = re.compile(r"[^\w\-.+]+", re.UNICODE)


def safe_name(s: str, max_len: int = 40) -> str:
    s = _SAFE.sub("_", s).strip("._")
    return (s or "layer")[:max_len]


def bgra_to_png(buf: bytes, w: int, h: int, path: Path) -> None:
    Image.frombytes("RGBA", (w, h), buf, "raw", "BGRA").save(path, "PNG")


def _safe_name_raw(lay) -> str:
    # lay.name is std::string -- pybind11 decodes as UTF-8 and may raise
    # on Japanese Photoshop PSDs (Shift-JIS / CP932).
    try:
        return lay.name
    except UnicodeDecodeError:
        return ""


def layer_to_dict(idx: int, lay) -> dict:
    raw = _safe_name_raw(lay)
    return {
        "index": idx,
        "name_raw": raw,
        "name": lay.name_unicode or raw,
        "layer_type": lay.layer_type.name,
        "blend_mode": lay.blend_mode.name,
        "opacity": lay.opacity,
        "fill_opacity": lay.fill_opacity,
        "clipping": lay.clipping,
        "visible": lay.visible,
        "transparency_protected": lay.transparency_protected,
        "bbox": {"top": lay.top, "left": lay.left,
                 "bottom": lay.bottom, "right": lay.right,
                 "width": lay.width, "height": lay.height},
        "layer_id": lay.layer_id,
        "channels": [{"id": ch.id, "length": ch.length} for ch in lay.channels],
    }


def export(psd_path: Path, out_dir: Path, mode: str) -> None:
    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "layers").mkdir(exist_ok=True)

    p = psdparse.PSDFile()
    if not p.load(str(psd_path)):
        raise SystemExit(f"failed to load: {psd_path}")

    layers_meta = [layer_to_dict(i, l) for i, l in enumerate(p.layers)]
    meta = {
        "source": str(psd_path),
        "header": {
            "version": p.header.version, "channels": p.header.channels,
            "width": p.header.width, "height": p.header.height,
            "depth": p.header.depth, "mode": p.header.mode,
        },
        "merged_alpha": p.merged_alpha,
        "layer_count": len(p.layers),
        "layers": layers_meta,
    }
    (out_dir / "layers.json").write_text(
        json.dumps(meta, ensure_ascii=False, indent=2), encoding="utf-8")

    bgra_to_png(p.merged_image(), p.header.width, p.header.height,
                out_dir / "merged.png")

    width = max(3, len(str(len(p.layers))))
    written = 0
    for i, l in enumerate(p.layers):
        if l.width <= 0 or l.height <= 0:
            continue
        if l.layer_type.name in ("FOLDER", "HIDDEN"):
            continue
        png = out_dir / "layers" / f"{i:0{width}d}_{safe_name(l.name_unicode or _safe_name_raw(l))}.png"
        bgra_to_png(p.layer_image(i, mode), l.width, l.height, png)
        written += 1

    print(f"wrote layers.json + merged.png + {written} layer PNG(s) to {out_dir}")


def main() -> None:
    ap = argparse.ArgumentParser(description="Dump a PSD as JSON + per-layer PNGs.")
    ap.add_argument("psd", type=Path)
    ap.add_argument("--out-dir", type=Path, default=None)
    ap.add_argument("--mode", choices=("masked", "image", "mask"), default="masked")
    args = ap.parse_args()
    out_dir = args.out_dir or args.psd.parent / f"{args.psd.stem}_export"
    export(args.psd, out_dir, args.mode)


if __name__ == "__main__":
    main()
