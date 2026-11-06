# Steam Workshop package

This directory contains the SCS Workshop Uploader version map and its universal informational fallback package.

The generated `160_content`, `161_content`, and `162_content` packages duplicate `src/mod`. Before uploading:

1. Copy `src/mod` to `workshop/160_content`, `workshop/161_content`, and `workshop/162_content`.
2. Remove `display_name` and `compatible_versions` from each content `manifest.sii`; Workshop obtains those values from the uploader and `versions.sii`.
3. Select this `workshop` directory in SCS Workshop Uploader.
4. Use `../assets/workshop_cover_640x360.jpg` as the preview image.

The published item is available at [Steam Workshop](https://steamcommunity.com/sharedfiles/filedetails/?id=3776052935).
