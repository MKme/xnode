# Basemap Plan

This file documents the basemap path that is now working in `Gen3`.

## Current watch-flash design

The active offline path is:

- host app uploads one PNG tile through XNODE
- tile is written to `/spiffs/osmmap/<z>/<x>/<y>.png`
- host app sends `installBasemap` with the center lon/lat/zoom
- watch persists that center/zoom
- `offline from watch flash` reopens that exact tile and scales it locally when the user changes zoom

This is intentionally different from a normal slippy-tile engine.

## Why this path exists

The old watch behavior could jump between different files at different zoom levels. If there were stale tiles in flash, the map would appear to switch to unrelated old imagery.

The watch-flash mode now treats the installed PNG as a fixed basemap image:

- no tile-server lookup on zoom change
- no fallback to another stored zoom tile
- marker projection stays consistent against the same image

## Commands involved

### `mapTile`

Host sends:

```json
{
  "path": "/spiffs/osmmap/10/301/385.png",
  "append": false,
  "offset": 0,
  "totalBytes": 1427,
  "data": "<base64url>"
}
```

Watch behavior:
- validates the path stays under `/spiffs/osmmap`
- creates parent directories if needed
- writes the decoded PNG bytes to SPIFFS

### `installBasemap`

Host sends:

```json
{
  "manifest": {
    "name": "Current AO",
    "tileRoot": "/spiffs/osmmap",
    "minZoom": 10,
    "maxZoom": 10,
    "center": {
      "lat": 43.65,
      "lon": -79.38,
      "zoom": 10
    }
  }
}
```

Watch behavior:
- stores the center lon/lat/zoom in config
- switches the map source to `offline from watch flash`

## Host apps

Current working host paths:

- `C:\GitHub\XTOC\xtoc-web\src\pages\XnodePage.tsx`
- `C:\GitHub\xcom\xcom\modules\xnode\xnode.js`

Both now do the same thing:

1. read current browser GPS
2. fetch one tile from the active raster template at zoom 10
3. shrink the PNG when needed to fit watch transport limits
4. upload with `mapTile`
5. activate with `installBasemap`

## Marker behavior

Marker placement now uses Web Mercator pixel projection in the firmware.

That matters because the watch is no longer switching between tile grids on zoom changes. The same installed image is scaled locally, so the marker transform has to scale against that same projected image.

Working now:
- watch current position marker
- external marker updates
- marker placement stays aligned while zooming the installed image

## Limits

- This is one installed center tile, not a scrolling offline atlas.
- SPIFFS is still small.
- Use small PNG tiles and keep expectations realistic.

For a larger offline area, SD-based multi-tile support can still exist as a separate path, but the XNODE watch-flash install path is now the stable tactical fallback for quick deployment.
