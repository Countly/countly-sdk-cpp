"use strict";

var fs = require("fs");
var path = require("path");
var assert = require("assert");
var cp = require("child_process");
var mocha = require("mocha");
var describe = mocha.describe;
var it = mocha.it;

var common = require("./common.js");
var server = require("./server.js");
var child;

describe(path.basename(__filename), function() {

  it("binaryPath should exist", function(done) {
    fs.access(common.binaryPath, done);
  });

  it("server should have no requests", function(done) {
    assert.equal(server.shift(), undefined);
    done();
  });

  it("binary should start", function(done) {
    child = cp.spawn(
      common.binaryPath,
      ["http://" + server.ip, server.port]
    );
    setTimeout(function() {
      done();
    }, 500);
  });

  it("check server request", function(done) {
    var json = server.shift();
    assert.equal(json.app_key, "ce894ea797762a11560217117abea9b1e354398c");
    assert.ok(json.device_id.length > 4);
    assert.equal(json.sdk_version, "1.4");
    assert.equal(json.begin_session, "1");
    assert.ok(json.metrics);
    done();
  });

  it("server should have no requests", function(done) {
    assert.equal(server.shift(), undefined);
    done();
  });

});
