# Tactical Map, GPS, and Overlays

The tactical map is the main visual link between XTOC/XCOM and XNODE.

## T-Deck Plus field view

![T-Deck Plus XNODE tactical map showing synced XTOC and XCOM markers on the larger screen](https://raw.githubusercontent.com/MKme/xnode/main/site/images/T-dec/IMG_6817.jpg)

The T-Deck Plus target now has the tactical map working on the larger 320x240 display. The view shows synced XTOC/XCOM markers on the active basemap, with the same overlay sync path as the watch targets but more room for controls and map context.

## Map source

The watch-flash map mode uses one installed raster basemap. XTOC/XCOM can clear and stream a current map tile/raster to the watch. The firmware stores the basemap and its projection metadata in SPIFFS, then displays it as the active tactical map.

Current constraints:

- One current raster basemap, not a stored library.
- Zoom is image scaling around the installed tile center.
- Panning is constrained by visible image bounds.
- SPIFFS is small, so host tooling must keep the raster compact.

## GPS position marker

The local user position marker is intended to be the most visible map marker:

- Rendered above other markers.
- Larger than tactical overlay markers.
- Shaped as a white directional triangle on Ultra/watch-flash paths.
- Rotated from heading data when available.

If there is no valid GPS fix, the map cannot place the user marker from receiver coordinates. The GPS status page is the first debug stop.

## Overlay marker model

XTOC/XCOM can push markers with host-projected `mapX`/`mapY` placement. If projected coordinates are not available, firmware falls back to Web Mercator lon/lat projection.

Supported marker types include:

- Team members.
- Mesh nodes.
- SITREPs.
- CONTACTs.
- TASKs.
- CHECKINs.
- Resource requests.
- Assets.
- Zones.
- Missions.
- Events.
- Phase lines.
- Sentinel.
- Routes.

The watch keeps up to 96 overlay markers. When the cache is full, the oldest slot is reused.

## Overlay persistence

Overlay markers persist in:

```text
/spiffs/osmmap/overlays.jsonl
```

The map reloads them when opened and after reboot. Replacement syncs are transactional: existing markers stay visible until an expected replacement batch arrives, and an intentional zero-count replacement clears the cache.

## Pan and zoom behavior

Recent fixes changed the map interaction model so marker hit targets do not break finger panning:

- Overlay marker objects are non-clickable.
- Local and external position marker images are non-clickable.
- Old invisible navigation pads are non-clickable.
- Raw touch deltas own drag/pan behavior.
- Visible zoom, exit, and layer controls remain clickable.

On the T-Deck Plus, base zoom still shows the square map with side bars when the raster itself is square. Zooming in uses the wider viewport and crops vertically, which makes better use of the 320x240 screen without rebuilding the map-loader pipeline. The large screen is the preferred display for inspecting dense synced XTOC/XCOM markers.

## GPS diagnostics

GPS status pages are meant to answer four questions:

- Is GPS powered?
- Is the UART path correct?
- Are raw bytes arriving?
- Are valid NMEA sentences and a fix being produced?

Useful interpretations:

- `Raw RX: 0`: receiver is silent, not powered, not wired, or wrong UART/pins/baud.
- Rising raw RX with bad NMEA: receiver is talking but parse/baud/noise/config is wrong.
- Valid NMEA with no fix: receiver is alive but lacks satellite lock.
- Valid fix/time: map position and GPS time sync can work.

T-Deck Plus supports both the LilyGO GPS Shield/L76K PCAS sequence and a u-blox/M10 fallback probe at `38400` and `9600`.
