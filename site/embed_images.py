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
}


def data_uri(mime: str, path: Path) -> str:
    return f"data:{mime};base64,{b64encode(path.read_bytes()).decode('ascii')}"


html = SOURCE.read_text(encoding="utf-8")

for source_path, (mime, asset_path) in ASSETS.items():
    html = html.replace(f'src="{source_path}"', f'src="{data_uri(mime, asset_path)}"')

TARGET.write_text(html, encoding="utf-8")
print(f"Wrote {TARGET}")
