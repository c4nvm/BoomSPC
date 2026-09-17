# Bundled fonts

Every `.ttf` / `.otf` in this folder shows up in **Settings > Fonts** as a
choice for the UI and the pattern grid. Drop more in and rebuild (the build
copies `assets/` next to the executable) or restart.

`fonts.txt` maps files to display names and a minimum size: a font with a
minimum is only offered once the size slider reaches it.

- `plok-small-font.otf`, `plok-big-font.otf`: FontStruct pixel fonts by
  "ParadigmTheGreat", Creative Commons Attribution Share Alike 3.0. See
  `license-plok.txt` and `readme-plok.txt`. The big one is uppercase only
  (no `#`, no lowercase) and is meant for large sizes.
- `mega-man-x.otf`, `quake-pc.otf`: FontStruct pixel fonts by Patrick H.
  Lauke, Creative Commons Attribution 3.0. See `license.txt` and
  `readme.txt`, which must travel with them. Pixel fonts look crispest at
  multiples of 8 px (16, 24, 32). (Colour fonts such as the Super E.D.F. one
  render only their outline layer here, which looks wrong, so they are not
  bundled.)
