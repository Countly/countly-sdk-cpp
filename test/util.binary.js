"use strict";

var fs = require("fs");
var path = require("path");
var assert = require("assert");
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

var binary;
var exited = false;

module.exports.start = function(server, cb) {

  assert(!binary);
  assert(!exited);

  binary = cp.spawn(
    filename,
    ["http://" + server.ip, server.port],
    { stdio: "pipe" }
  );

  binary.on("exit", function() {
    exited = true;
  });

  setTimeout(function() {
    cb();
  }, 500);

}

module.exports.stop = function(cb) {

  assert(binary);
  assert(!exited);

  binary.stdin.write("q");

  setTimeout(function() {
    assert(exited);
    binary = null;
    exited = false;
    cb();
  }, 1500);

}

module.exports.command = function(c) {

  assert(binary);
  assert(!exited);

  binary.stdin.write(c);

}

if (!module.parent) {
  console.log(filename);
}
