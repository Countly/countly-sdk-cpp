#!/usr/bin/python

{ "targets": [

###############################################################################
###############################################################################
###############################################################################

  {
    "target_name": "libCountlyCpp",
    "target_dir": "lib",
    "type": "static_library",
    "sources": [
      "Countly.cpp",
      "CountlyConnectionQueue.cpp",
      "CountlyEventQueue.cpp",
      # you may add "sqlite3.c" here
    ],
    "defines": [
      "NOSQLITE",
    ],
  },

###############################################################################
###############################################################################
###############################################################################

  {
    "target_name": "CountlyCppTest",
    "type": "executable",
    "sources": [
      "Countly.cpp",
      "CountlyConnectionQueue.cpp",
      "CountlyEventQueue.cpp",
      "CountlyTest.cpp",
      # you may add "sqlite3.c" here
    ],
    "conditions": [
      ["OS=='win'", {
        "libraries": [
          "-lwinhttp",
        ],
      }],
      ["OS=='linux'", {
        "libraries": [
          "-lcurl",
        ],
      }],
    ],
    "defines": [
      "NOSQLITE",
    ],
  },

###############################################################################
###############################################################################
###############################################################################

  {
    "target_name": "CountlyCppTestStatic",
    "type": "executable",
    'dependencies': [
      "libCountlyCpp",
    ],
    "sources": [
      "CountlyTest.cpp",
    ],
    "conditions": [
      ["OS=='win'", {
        "libraries": [
          "-llibCountlyCpp.lib",
          "-lwinhttp",
        ],
        "configurations": {
          "Debug": {
            "msvs_settings": {
              "VCLinkerTool": {
                "AdditionalLibraryDirectories": [
                  "Debug/lib",
                ],
              },
            },
          },
          "Release": {
            "msvs_settings": {
              "VCLinkerTool": {
                "AdditionalLibraryDirectories": [
                  "Release/lib",
                ],
              },
            },
          },
        },
      }],
      ["OS=='linux'", {
        "libraries": [
          "-lcurl",
        ],
      }],
    ],
  },

###############################################################################
###############################################################################
###############################################################################

] }
