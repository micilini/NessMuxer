# Third-party dependencies

This folder is reserved for optional local SDKs/libraries used during development.

This repository does **not** redistribute third-party proprietary or GPL materials here.

Examples:
- NVIDIA Video Codec SDK headers
- x264 headers/libraries

Typical local layout:

```text
third_party/
  nvenc/
    include/
  x264/
    include/
    lib/
```

Obtain these dependencies directly from their official sources and review their licenses before building or distributing binaries that use them.