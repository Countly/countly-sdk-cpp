"use strict";

var path = require("path");

module.exports.binaryPath = (process.platform === "win32"
  ? path.join(__dirname, "../bin/CountlyTest.exe")
  : path.join(__dirname, "../bin/CountlyTest")
);
