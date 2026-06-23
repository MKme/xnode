from base64 import b64encode
from pathlib import Path


ROOT = Path(__file__).resolve().parent
SOURCE = ROOT / "xnode-sales-page-linked.html"
TARGET = ROOT / "xnode-sales-page-embedded.html"

ASSETS = {
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/xnode-hero.webp": (
        "image/webp",
        ROOT / "images" / "xnode-hero.webp",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/xnode-watch-screens.webp": (
        "image/webp",
        ROOT / "images" / "xnode-watch-screens.webp",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/xnode-system-flow.svg": (
        "image/svg+xml",
        ROOT / "images" / "xnode-system-flow.svg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/IMG_6786.jpg": (
        "image/jpeg",
        ROOT / "images" / "IMG_6786.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/IMG_6765.jpg": (
        "image/jpeg",
        ROOT / "images" / "IMG_6765.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/IMG_6772.jpg": (
        "image/jpeg",
        ROOT / "images" / "IMG_6772.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/IMG_6740.jpg": (
        "image/jpeg",
        ROOT / "images" / "IMG_6740.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6809.jpg": (
        "image/jpeg",
        ROOT / "images" / "T-dec" / "IMG_6809.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6810.jpg": (
        "image/jpeg",
        ROOT / "images" / "T-dec" / "IMG_6810.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6811.jpg": (
        "image/jpeg",
        ROOT / "images" / "T-dec" / "IMG_6811.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6812.jpg": (
        "image/jpeg",
        ROOT / "images" / "T-dec" / "IMG_6812.jpg",
    ),
    "https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6813.jpg": (
        "image/jpeg",
        ROOT / "images" / "T-dec" / "IMG_6813.jpg",
    ),
}


def data_uri(mime: str, path: Path) -> str:
    return f"data:{mime};base64,{b64encode(path.read_bytes()).decode('ascii')}"


html = SOURCE.read_text(encoding="utf-8")

for source_path, (mime, asset_path) in ASSETS.items():
    html = html.replace(f'src="{source_path}"', f'src="{data_uri(mime, asset_path)}"')

TARGET.write_text(html, encoding="utf-8")
print(f"Wrote {TARGET}")
