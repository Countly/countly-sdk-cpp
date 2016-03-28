"use strict";

var fs = require("fs");
var path = require("path");
var assert = require("assert");
var mocha = require("mocha");
var describe = mocha.describe;
var it = mocha.it;

var binary = require("./util.binary.js");
var server = require("./util.server.js");
var compare = require("./util.compare.js");

describe(path.basename(__filename), function() {

  it("binary should start", function(done) {
    binary.start(server, done);
  });

  it("binary should exit", function(done) {
    binary.stop(done);
  });

});
