#!/usr/bin/env python3
"""Check that the tactical home documentation ships real, linked screen assets."""
from html.parser import HTMLParser
from pathlib import Path
import json
import struct

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "site/images/home-tactical-2026-09-28"
EXPECTED = {
    "tdeck-plus-device.png": (320, 240),
    "watch-ultra-dark.png": (362, 440),
    "watch-s3-dark.png": (240, 240),
    "tdeck-pro-light.png": (240, 320),
    "tdeck-plus-dark-apps.png": (320, 240),
    "tdeck-plus-dark-setup.png": (320, 240),
    "watch-s3-dark-apps.png": (240, 240),
    "tdeck-pro-light-apps.png": (240, 320),
}


class StoreImages(HTMLParser):
    def __init__(self):
        super().__init__()
        self.anchor = None
        self.images = []
        self.menus = []

    def handle_starttag(self, tag, attrs):
        attrs = dict(attrs)
        if tag == "a":
            self.anchor = attrs
        if tag == "img" and "Field Chronometer" in attrs.get("alt", ""):
            self.images.append((attrs, self.anchor))
        if tag == "img" and "menu firmware rendering" in attrs.get("alt", ""):
            self.menus.append((attrs, self.anchor))

    def handle_endtag(self, tag):
        if tag == "a":
            self.anchor = None


def check():
    inventory = json.loads((ASSETS / "manifest.json").read_text(encoding="utf-8"))
    screenshots = inventory.get("screenshots", [])
    assert screenshots, "Empty home screenshot inventory"
    names = set()
    for entry in screenshots:
        name = entry["file"]
        assert name == Path(name).name and name not in names, "Invalid/duplicate screenshot inventory path"
        names.add(name)
        data = (ASSETS / name).read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", f"Not a PNG: {name}"
        assert struct.unpack(">II", data[16:24]) == (entry["width"], entry["height"]), f"Inventory dimensions wrong: {name}"
    assert set(EXPECTED).issubset(names), "Published screenshot absent from inventory"
    readme = (ROOT / "README.md").read_text(encoding="utf-8")
    for name, dimensions in EXPECTED.items():
        path = ASSETS / name
        data = path.read_bytes()
        assert data[:8] == b"\x89PNG\r\n\x1a\n", f"Not a PNG: {path}"
        assert struct.unpack(">II", data[16:24]) == dimensions, f"Wrong resolution: {path}"
        assert name in readme, f"Unreferenced home screen: {name}"
    page = (ROOT / "site/xnode-sales-page-linked.html").read_text(encoding="utf-8")
    parser = StoreImages()
    parser.feed(page)
    assert len(parser.images) == 3, "Store must show Ultra, Plus and Pro home screenshots"
    assert len(parser.menus) == 3, "Store must show current Plus Apps/Setup and Pro Apps menus"
    sources = set()
    for attrs, anchor in parser.images + parser.menus:
        src = attrs.get("src", "")
        assert src and src not in sources, "Empty or repeated store screenshot"
        sources.add(src)
        assert anchor and anchor.get("href") == src, "Missing full-size image link"
        assert anchor.get("aria-label", "").startswith("View full-size"), "Unnamed image link"
        assert anchor.get("tabindex") != "-1", "Image link is not keyboard focusable"
        assert int(attrs["width"]) > 0 and int(attrs["height"]) > 0, "Missing dimensions"
        assert "concept" not in src.lower(), "Concept art cannot replace a firmware screenshot"
    assert "illustrative readings" in page, "Home screenshot context missing"
    assert "representative tool selections" in page, "Native menu capture context missing"
    for filename in ("USAGE.md", "wiki/Home.md", "wiki/XNODE-Firmware.md"):
        text = (ROOT / filename).read_text(encoding="utf-8")
        assert "tdeck-plus-device.png" in text, f"Missing current home in {filename}"
    print("Home documentation: native image dimensions, references and full-size links verified")


if __name__ == "__main__":
    check()
