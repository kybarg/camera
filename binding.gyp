{
  "targets": [
    {
      "target_name": "addon",
      "cflags!": [
        "-fno-exceptions"
      ],
      "cflags_cc!": [
        "-fno-exceptions"
      ],
      "sources": [
        "addon.cc",
        "camera.cc",
        "capture.cc"
      ],
      "libraries": [
        "-lmf",
        "-lmfplat",
        "-lMfreadwrite",
        "-lMfuuid",
        "-lwmcodecdspuuid",
        "-lole32",
        "-luuid",
        "-lstrmiids",
        "-lshlwapi"
      ],
      "include_dirs": [
        "<!@(node -p \"require('node-addon-api').include\")"
      ],
      "defines": [
        "NAPI_DISABLE_CPP_EXCEPTIONS"
      ],
      "msvs_settings": {
        "VCCLCompilerTool": {
          "ExceptionHandling": 1
        }
      }
    }
  ]
}
