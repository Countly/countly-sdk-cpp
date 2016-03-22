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
      "WIN32_LEAN_AND_MEAN",
      "NOSSL",
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
          "-lws2_32",
        ],
      }],
    ],
    "defines": [
      "WIN32_LEAN_AND_MEAN",
      "NOSSL",
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
          "-lDebug/lib/libCountlyCpp.lib",
          "-lws2_32",
        ],
      }],
    ],
  },

###############################################################################
###############################################################################
###############################################################################

] }
