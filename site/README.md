# XNODE store page

`xnode-sales-page-linked.html` is the maintained source for the custom HTML on:

https://store.mkme.org/xnode/

The page is intentionally stored as standalone HTML and CSS. On WordPress it is
saved as ordinary page content rather than an Elementor document so WordPress
does not serve stale Elementor widget data.

## T-Deck Pro media

The original photos remain versioned in `site/images/tdeck pro/`. The deployed
store page uses Media Library copies so the storefront does not depend on
percent-encoded GitHub paths:

- `IMG_7371.jpg` → `xnode-tdeck-pro-clock.jpg` (WordPress media ID 37448)
- `IMG_7372.jpg` → `xnode-tdeck-pro-launcher.jpg` (WordPress media ID 37449)
- `IMG_7373.jpg` → `xnode-tdeck-pro-messaging.jpg` (WordPress media ID 37450)

## Deployment notes

1. Use the linked HTML file as the complete page body.
2. Keep the markup compact when saving through the WordPress REST API; this
   prevents automatic paragraph and line-break insertion.
3. Leave `_elementor_edit_mode` and `_elementor_data` empty for this page.
4. Purge the Elementor generated-file cache and WP-Optimize page cache.
5. Verify the public page, all image responses, and the mobile photo-grid
   breakpoint after deployment.
