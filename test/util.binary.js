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

module.exports.start = function(server, cb) {

  assert(!binary);

  [ "../countly.deviceid",
    "../countly.sqlite",
    "../countly.sqlite-journal"
  ].some(function(trash) {
    var full = path.join(__dirname, trash);
    try { fs.unlinkSync(full); } catch (_) {}
  });

  if (process.env.COUNTLY_VALGRIND) {

    binary = cp.spawn(
      "valgrind",
      [ "--suppressions=" + path.resolve(__dirname, "..", "CountlyCpp.supp"),
        "--leak-check=full", "--show-leak-kinds=all",
        filename, "http://" + server.ip, server.port],
      { stdio: [ "pipe", "inherit", "inherit" ] }
    );

    setTimeout(function() {
      cb();
    }, 1500);

  } else {

    binary = cp.spawn(
      filename,
      ["http://" + server.ip, server.port],
      { stdio: [ "pipe", "inherit", "inherit" ] }
    );

    setTimeout(function() {
      cb();
    }, 500);

  }

}

module.exports.stop = function(cb) {

  assert(binary);

  binary.on("exit", function() {
    assert(binary);
    assert.equal(binary.exitCode, 0);
    binary = null;
    cb();
  });

  binary.stdin.write("q");

}

module.exports.command = function(c) {

  assert(binary);
  binary.stdin.write(c);

}

if (!module.parent) {
  console.log(filename);
}
