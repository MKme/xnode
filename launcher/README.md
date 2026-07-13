# Launcher assets

Use `xnode-launcher-icon-512.png` as the preferred Launcher catalog cover/icon for XNODE firmware.

Catalog image URLs after merge:

- `https://raw.githubusercontent.com/MKme/xnode/main/launcher/xnode-launcher-icon-512.png`
- `https://raw.githubusercontent.com/MKme/xnode/main/launcher/xnode-launcher-icon-256.png`
- `https://raw.githubusercontent.com/MKme/xnode/main/launcher/xnode-launcher-icon-96.png`

Source:

- `xnode-launcher-icon.svg`

Suggested catalog metadata field:

```json
{
  "cover": "https://raw.githubusercontent.com/MKme/xnode/main/launcher/xnode-launcher-icon-512.png"
}
```

Release asset naming:

- `xnode-t-watch-ultra-launcher-<timestamp>.bin`
- `xnode-t-watch-s3-gen3-launcher-<timestamp>.bin`
- `xnode-tdeck-plus-launcher-<timestamp>.bin`
- `xnode-tdeck-pro-launcher-<timestamp>.bin`

Local build outputs remain `.pio/build/<env>/firmware.bin`; rename the release assets by target so Launcher catalogs do not cross-flash the wrong board.
