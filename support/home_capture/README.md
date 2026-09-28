# Native home validation

`python support/home_capture/capture.py --check-only` compiles the actual
`main_tile.cpp`, `widget_styles.cpp` and their local headers against the existing PlatformIO LVGL v7
dependency and bundled firmware fonts. It runs four device resolutions in light
and dark themes, checking navigation callbacks, original message-widget event
forwarding, radio state labels, hidden duplicate battery/time metadata, sensor visibility, clock format,
three-widget layout, label bounds and clock/date/moon separation.
It also checks actual shared background styles and bound legacy style copies
across repeated light/dark switches, including the home callback updates.

Pointer tests use an actual LVGL pointer input driver and press/release samples
over Messages text, envelope, badge and edges, plus visible navigation labels
and edges. Mainbar slide registration performs the real `lv_page_glue_obj`
operation. An additional fault-injection probe reproduces the old glued-label
tap interception after untouched production objects have passed. The original
widget callback and destination are spies; destination screen behavior still
needs device testing. Earlier direct-event-only tests did not cover hit testing.

`python support/home_capture/capture.py --navigation --check-only` additionally
compiles the actual mainbar, Apps/Setup tile builders, app/setup registration,
shared styles and tactical icon renderer. It exercises horizontal pointer swipes
through occupied root pages and a 2x2 app allocation flattened into four pages;
numeric IDs, activation/hibernate callbacks, jump/back and history bounds are
checked. Pointer clicks cover icon/card/caption/margin and handlers installed
after registration. It also verifies coordinate-wrap rejection, isolated group
boundaries and caption/footer separation. Navigation captures without
`--check-only` go to the private evidence directory, alongside source hashes.
They use selected real registration labels and bundled art; application screen
bodies and the physical touch controller are not simulated.

`python support/home_capture/capture.py --messages --check-only` follows a real
pointer from the home notification row through actual widget removal and
mainbar navigation. It compiles the current message entry, open-latest,
mark-read, dismissal, activation and hibernation function bodies extracted
verbatim from `bluetooth_message.cpp`. It asserts the actual active message
tile, retained unread state when navigation is rejected, and repeated open/back
cycles. A native-only fault recreates the old pre-navigation dismissal plus
history-loop guard failure. Message content rendering, message data, navigation
button configuration and fade effects remain isolated fixtures; this is not a
claim of full Bluetooth ingestion or message-body rendering coverage.

Requires an existing clang/clang++ or gcc/g++ installation and the already
installed `.pio/libdeps/tdeck-plus/lvgl` tree. It never installs dependencies.
The check-only command needs only the Python standard library. Compilation
artifacts and intermediate PPM framebuffers are removed automatically.

Without `--check-only`, the script uses the existing Pillow installation to
write eight documentation PNGs and their public image inventory. Capture
provenance and source hashes go to the workspace's private
`vault/xnode-home-tactical-2026-09-28/native-capture-evidence.json`.

These are software framebuffer renders of the actual home implementation, with
deterministic demonstration values. Hardware services and destination screens
are stubbed; the global status bar is outside this focused test. This does not
verify a physical touch panel, radio, display refresh, or complete destination
workflow. Hardware capture remains a separate check.

## Mesh Chat native validation

`python support/home_capture/capture.py --mesh --check-only` executes the actual
Mesh Chat source, bounded MeshHistory, mainbar, and software keyboard with real
LVGL pointer input for all four display sizes and both themes. Radio/physical
keyboard/NVS/clock services are controlled fixtures; no RF transmission occurs.
Omit `--check-only` to retain private screenshots under
`C:/GitHub/vault/xnode-mesh-chat-2026-09-28`; this does not publish product images.

The workflow covers first-open examples, isolated previews, rejected/accepted
send drafts and history, sparse channel-slot mapping and channel-specific drafts,
24-record history/long bodies, reading-position preservation on ring rollover,
new-message recovery, left/right navigation, actual watch keyboard key pages and
OK/Cancel, navigation persistence, and a second fresh process with saved examples
OFF. Hardware keyboard input and actual NVS/radio behavior require board testing.
