"use strict";

var fs = require("fs");
var path = require("path");
var binary;

[ "../Debug/CountlyCppTest.exe",
  "../Debug/CountlyCppTestStatic.exe",
  "../Release/CountlyCppTest.exe",
  "../Release/CountlyCppTestStatic.exe",
  "../out/Debug/CountlyCppTest",
  "../out/Debug/CountlyCppTestStatic",
  "../out/Release/CountlyCppTest",
  "../out/Release/CountlyCppTestStatic"
].some(function(candidate) {
  var full = path.join(__dirname, candidate);
  if (fs.existsSync(full)) {
    binary = full;
    return true;
  }
});

module.exports = binary;

if (!module.parent) {
  console.log(binary);
}
