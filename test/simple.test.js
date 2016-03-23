"use strict";

var fs = require("fs");
var path = require("path");
var assert = require("assert");
var mocha = require("mocha");
var describe = mocha.describe;
var it = mocha.it;

var binary = require("./util.binary.js");
var server = require("./util.server.js");
var events = require("./util.events.js");
var metrics = require("./util.metrics.js");

describe(path.basename(__filename), function() {

  it("start correct server", function(done) {
    server.start("correct");
    done();
  });

  var child;
  var binary_exited = false;

  it("binary should start", function(done) {
    child = binary.spawn(server);
    child.on("exit", function() {
      binary_exited = true;
    });
    setTimeout(function() {
      done();
    }, 500);
  });

  it("make 3 events", function(done) {
    child.stdin.write("0");
    child.stdin.write("1");
    child.stdin.write("2");
    done();
  });

  it("wait 2 seconds", function(done) {
    this.timeout(3 * 1000);
    setTimeout(function() {
      done();
    }, 2 * 1000);
  });

  it("binary should exit", function(done) {
    child.stdin.write("q");
    setTimeout(function() {
      assert.equal(binary_exited, true);
      done();
    }, 1500);
  });

  var app_key = "ce894ea797762a11560217117abea9b1e354398c";
  var device_id;

  it("check begin_session", function(done) {
    var json = server.shift();
    assert.equal(json.app_key, app_key);
    assert.ok(json.device_id.length > 4);
    device_id = json.device_id;
    assert.equal(json.sdk_version, "1.4");
    assert.equal(json.begin_session, "1");
    metrics.test(json.metrics);
    done();
  });

  it("check keepalive", function(done) {
    var json = server.shift();
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
    assert.equal(json.session_duration, "30");
    done();
  });

  it("check events pack", function(done) {
    var json = server.shift();
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
    assert.equal(json.events.length, 3);
    events.test(json.events[0], 0);
    events.test(json.events[1], 1);
    events.test(json.events[2], 2);
    done();
  });

  it("check end_session", function(done) {
    var json = server.shift();
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
    assert.equal(json.end_session, "1");
    var json = server.shift();
    done();
  });

  it("server should have no requests", function(done) {
    assert.equal(server.shift(), undefined);
    done();
  });

  it("stop server", function(done) {
    server.stop();
    done();
  });

});
