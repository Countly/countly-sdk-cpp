"use strict";

var fs = require("fs");
var path = require("path");
var cp = require("child_process");
var filename;

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
    filename = full;
    return true;
  }
});

module.exports.spawn = function(server) {
  return cp.spawn(
    filename,
    ["http://" + server.ip, server.port],
    { stdio: "pipe" }
  );
}

if (!module.parent) {
  console.log(filename);
}
