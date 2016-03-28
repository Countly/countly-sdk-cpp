"use strict";

var path = require("path");
var cp = require("child_process");
var assert = require("assert");
var mocha = require("mocha");
var describe = mocha.describe;
var it = mocha.it;

describe(path.basename(__filename), function() {

  it("lint code style", function(done) {

    this.timeout(30 * 1000);

    var linter = cp.spawnSync("python", [
      path.join(__dirname, "../vendor/styleguide/cpplint/cpplint.py"),
      "--verbose=0",
      "Countly.h", "Countly.cpp",
      "CountlyConnectionQueue.h", "CountlyConnectionQueue.cpp",
      "CountlyEventQueue.h", "CountlyEventQueue.cpp"
    ], { stdio: "pipe" });

    if (linter.status) {
      console.log("\n" + linter.stderr.toString());
      return done(new Error("Linter non-zero status"));
    }

    done();

  });

});
