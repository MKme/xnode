# Mesh Chat modernization concept

Historical design record. The final production implementation removes Examples/Preview and opens directly into live chat. Watches use separate conversation, WRITE/review, and RADIO pages with larger text and controls; handhelds retain the compact two-page layout. Later watch layout builds supersede the initial watch implementation described below.

Phase: locally verified, 28 September 2026. Owner approved the concept and required a functional watch-specific layout as well as the larger T-Deck chat view.

Owner requested the tactical modernization skill for Mesh because the existing compose screen does not expose a chat conversation. Existing home/menu work remains intact. Implementation and validation cover both watches and both T-Decks; physical flashing is limited to the confirmed attached T-Deck Plus.

## Proposal

- Open MESH directly into a channel conversation, with incoming and outgoing messages, sender identifiers, local timestamps and truthful transmission state.
- Keep the channel selector and a pinned composer with the existing 80-character UI limit. Preserve hardware keyboard input and on-screen keyboard access on watches.
- Move local node ID, last-peer RSSI/SNR, frequency and other radio details onto a neighboring RADIO page. Page navigation stays left/right; message history scrolls vertically.
- Match the shared Mission Teal background, retain light theme and monochrome Pro support, and keep battery only in the global status bar.
- Provide the skill-required yellow EXAMPLES control with a visibly labeled, isolated sample conversation on first use. Sample replies are local previews, never transmissions; preserve the saved off preference. The concept illustrates the live composer using synthetic conversation text, not real received packets.
- Preserve draft/scroll context during navigation and maintain bounded in-session conversation history. Do not silently add flash storage of sensitive message bodies.

## Source findings and implementation inventory

| Intent | Existing source / gap | Proposed implementation and validation |
| --- | --- | --- |
| Read conversation | meshtastic_app.cpp has a compose field and Inbox delegates to bluetooth_message_open | Dedicated mesh timeline; test incoming and outgoing content, ordering, empty state, wrapping, scrolling and unread behavior |
| Choose channel | service channel count/name/active-channel APIs already exist | Filter conversation by actual channel slot; validate list-index versus slot mapping and disabled channels |
| Send reply | send_text broadcasts on active channel; send_text_to also exists | Preserve broadcast workflow and real send handler; keep draft on rejected send and track asynchronous local TX result, never infer recipient delivery |
| Receive text | service exposes RX callback with from/to/channel/packet ID/RSSI/SNR/text | Retain metadata in bounded history without replacing an existing callback consumer; do not conflate generic phone notifications with mesh chat |
| Inspect radio | Existing node/status/last-peer/frequency readbacks | Separate RADIO page using existing service values; distinguish unavailable state from zero or healthy state |
| Navigate/type | Existing mainbar and keyboard paths | Test actual pointer, keyboard, swipe, Back, and lifecycle paths on all four board layouts |
| Example mode | New requirement from modernization skill | Isolate examples from live history and RF transmit; verify toggle persistence and real records unchanged |

The source currently exposes latest sender/text readbacks, not a complete chat history API. A real chat client therefore needs service/history integration as well as the visible layout. Radio TX initiation is not delivery confirmation. No new radio protocol, dependency, cloud service, read receipts or encryption claim is proposed.

## Artwork

`mesh-chat-concept.png` was generated using the built-in image generation tool; reproducible prompt is `render-prompt.txt`. Existing home screenshot was the style reference. Conversation and radio status are illustrative. The original tool output is retained in the tool-managed generated_images directory; this repository contains the canonical project copy.

After approval: implement, validate the real retained/new workflows, independently review GUI and software functionality, rebuild and flash the confirmed T-Deck Plus, and refresh affected documentation. Public release/Git publishing is not implied.

## Implementation result

All four firmware targets compiled. The attached T-Deck Plus was flashed and its actual Mesh Chat display captured over USB. Eight board/theme native UI variants pass pointer, keyboard, channel/draft, history and navigation tests; independent GUI 9.5/10 and functionality 9.6/10 within that exercised scope. Physical watch hardware and over-the-air recipient delivery remain unverified. Product screenshots are in `site/images/mesh-chat-2026-09-28`; application binaries and checksums are in `artifacts/mesh-chat-20260928.json`. No public firmware release or Git push is implied.
