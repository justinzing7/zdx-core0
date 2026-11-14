# Assets: bitmaps, fonts, animations

## Pipeline

```
assets/src/*.png  ──tools/img2c──►  src/assets/gen/<name>.h   (const Bitmap kBmpName)
assets/src/anim/<name>/NN.png ──►   src/assets/gen/anim_<name>.h (Bitmap frames + Animation)
assets/fonts/*.bdf|ttf ──(u8g2 / fontconvert)──► src/assets/gen/font_<name>.h
```

- **Bitmap format:** 1 bpp, row-major, MSB first, rows padded to a byte (`Bitmap{w, h, data}`). This is what `Gfx::bitmap` expects; adapters convert if their library differs (for example U8g2 XBM is LSB-first).
- **Generated files** are committed so a build never requires Python, and are regenerated with `python tools/img2c.py` (needs Pillow).
- **Animations:** the frame directory plus an optional `anim.json` (`{"hold_ms": 80, "loop": false}` or per-frame holds). Output is an `AnimFrame[]` and an `Animation`.
- Everything is `const`, so it stays in flash. On ESP32, large assets can live in a flash partition instead.

## Naming

`kBmp<Name>`, `kAnim<Name>`, `kFont<Name><Size>`. Icons are designed on an 8 px grid (8×8, 16×16) for status bars and menus.

## Brand

Splash animations and the logo live in a shared `zingcore-assets` folder or repo, so every product boots with a consistent identity. Product-specific art stays in the product repo.

## Acceptance

- [ ] Regenerating assets from source reproduces the committed headers byte-for-byte.
- [ ] Splash animation frames total ≤ the flash budget set in the product plan.
