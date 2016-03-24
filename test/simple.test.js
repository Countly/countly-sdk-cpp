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

  it("binary should start", function(done) {
    binary.start(server, done);
  });

  it("make 3 events", function(done) {
    binary.command("0");
    binary.command("1");
    binary.command("2");
    done();
  });

  it("wait 2 seconds", function(done) {
    this.timeout(3 * 1000);
    setTimeout(function() {
      done();
    }, 2 * 1000);
  });

  it("binary should exit", function(done) {
    binary.stop(done);
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
