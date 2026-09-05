# Steam Workshop package

This directory contains the SCS Workshop Uploader version map and its universal informational fallback package.

The generated `160_content` package is intentionally excluded from Git because it duplicates `src/mod`. Before uploading:

1. Copy `src/mod` to `workshop/160_content`.
2. Remove `display_name` and `compatible_versions` from `workshop/160_content/manifest.sii`; Workshop obtains those values from the uploader and `versions.sii`.
3. Select this `workshop` directory in SCS Workshop Uploader 1.60.1.
4. Use `../assets/workshop_cover_640x360.jpg` as the preview image.

The published item is available at [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3776052935).
