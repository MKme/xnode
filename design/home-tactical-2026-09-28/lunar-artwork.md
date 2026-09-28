# Lunar illustration source

`lunar-texture-source.png` is an original built-in ImageGen lunar surface illustration. Its reproducible prompt is `lunar-texture-prompt.txt`. It is generic display artwork, not a live image or a precision lunar atlas.

`support/generate_home_moon_texture.py` compiles the local illustration into the 64x64 luminance table in `src/gui/mainbar/main_tile/home_moon_texture.h`. The firmware applies its existing calculated phase boundary and illumination. Color displays use the texture; e-paper keeps the binary procedural phase indicator. No network image, font or runtime download is required.
