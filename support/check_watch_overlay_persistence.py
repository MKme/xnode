#!/usr/bin/env python3
"""CI guard for XNODE watch tactical map marker persistence wiring."""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
MAP_CPP = ROOT / "src" / "app" / "osmmap" / "osmmap_app_main.cpp"
MAP_H = ROOT / "src" / "app" / "osmmap" / "osmmap_app_main.h"
BLE_CPP = ROOT / "src" / "hardware" / "ble" / "xnode.cpp"


def read(path: Path) -> str:
    return path.read_text(encoding="utf-8")


def command_branch(source: str, command: str) -> str:
    marker = f'if ( strcmp( type, "{command}" ) == 0 )'
    start = source.index(marker)
    next_start = source.find('\n            if ( strcmp( type, "', start + len(marker))
    return source[start:] if next_start == -1 else source[start:next_start]


def assert_contains(source: str, needle: str, name: str) -> None:
    if needle not in source:
        raise AssertionError(f"{name} missing {needle!r}")


def assert_not_contains(source: str, needle: str, name: str) -> None:
    if needle in source:
        raise AssertionError(f"{name} still contains {needle!r}")


def assert_before(source: str, earlier: str, later: str, name: str) -> None:
    early_index = source.index(earlier)
    late_index = source.index(later)
    if early_index > late_index:
        raise AssertionError(f"{name} expected {earlier!r} before {later!r}")


def main() -> None:
    map_cpp = read(MAP_CPP)
    map_h = read(MAP_H)
    ble_cpp = read(BLE_CPP)

    assert_contains(map_h, "bool osmmap_save_overlay_items( void );", "public map API")
    assert_contains(map_h, "bool osmmap_load_overlay_items( void );", "public map API")
    assert_contains(map_h, "void osmmap_clear_persisted_overlay_items( void );", "public map API")
    assert_contains(map_h, "void osmmap_begin_overlay_replace( void );", "public map API")
    assert_contains(map_h, "void osmmap_commit_overlay_replace( void );", "public map API")
    assert_contains(map_h, "void osmmap_cancel_overlay_replace( void );", "public map API")

    assert_contains(map_cpp, 'OSMMAP_OVERLAY_CACHE_PATH = "/spiffs/osmmap/overlays.jsonl"', "overlay cache path")
    assert_contains(map_cpp, 'fopen( OSMMAP_OVERLAY_CACHE_PATH, "wb" )', "overlay cache save")
    assert_contains(map_cpp, 'fopen( OSMMAP_OVERLAY_CACHE_PATH, "rb" )', "overlay cache load")
    assert_contains(map_cpp, "remove( OSMMAP_OVERLAY_CACHE_PATH );", "overlay cache clear")
    assert_contains(map_cpp, "osmmap_load_overlay_items();", "map setup")
    assert_contains(map_cpp, "osmmap_overlay_replace_active", "live overlay replace")
    assert_contains(map_cpp, "void osmmap_begin_overlay_replace( void )", "live overlay replace")
    assert_contains(map_cpp, "void osmmap_commit_overlay_replace( void )", "live overlay replace")

    sync = command_branch(ble_cpp, "syncState")
    assert_not_contains(
        sync,
        "osmmap_clear_overlay_items();\n                    xnode_overlay_sync_replacing = expected_overlays > 0;",
        "syncState replace",
    )
    assert_contains(sync, "osmmap_begin_overlay_replace();", "syncState replace")
    assert_contains(sync, "if ( expected_overlays == 0 )", "syncState replace")
    assert_contains(sync, "osmmap_clear_overlay_items();", "syncState replace")
    assert_contains(sync, "osmmap_clear_persisted_overlay_items();", "syncState replace")
    assert_before(sync, "osmmap_clear_overlay_items();", "osmmap_clear_persisted_overlay_items();", "syncState replace")

    overlay_batch = command_branch(ble_cpp, "overlayBatch")
    assert_contains(overlay_batch, "osmmap_commit_overlay_replace();", "overlayBatch")
    assert_contains(overlay_batch, "osmmap_save_overlay_items();", "overlayBatch")
    assert_before(overlay_batch, "osmmap_commit_overlay_replace();", "osmmap_save_overlay_items();", "overlayBatch")
    assert_before(overlay_batch, "osmmap_save_overlay_items();", 'xnode_send_event( "overlayBatchAck", reply );', "overlayBatch")

    packet_batch = command_branch(ble_cpp, "packetBatch")
    assert_contains(packet_batch, "osmmap_commit_overlay_replace();", "packetBatch")
    assert_contains(packet_batch, "osmmap_save_overlay_items();", "packetBatch")
    assert_before(packet_batch, "osmmap_commit_overlay_replace();", "osmmap_save_overlay_items();", "packetBatch")
    assert_before(packet_batch, "osmmap_save_overlay_items();", 'xnode_send_event( "packetBatchAck", reply );', "packetBatch")

    clear_basemap = command_branch(ble_cpp, "clearBasemap")
    assert_contains(clear_basemap, "osmmap_clear_overlay_items();", "clearBasemap")
    assert_contains(clear_basemap, "osmmap_clear_persisted_overlay_items();", "clearBasemap")
    assert_contains(clear_basemap, "osmmap_cancel_overlay_replace();", "clearBasemap")

    print("watch overlay persistence wiring ok")


if __name__ == "__main__":
    main()
