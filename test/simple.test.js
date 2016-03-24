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
    setTimeout(done, 2 * 1000);
  });

  it("binary should exit", function(done) {
    binary.stop(done);
  });

  var device_id;

  it("check begin_session", function(done) {
    var json = server.shift();
    device_id = compare(json, "begin_session");
    done();
  });

  it("check keepalive", function(done) {
    var json = server.shift();
    compare(json, "keepalive", device_id);
    done();
  });

  it("check events pack", function(done) {
    var json = server.shift();
    compare(json, "event", device_id);
    assert.equal(json.events.length, 3);
    compare(json.events[0], "0");
    compare(json.events[1], "1");
    compare(json.events[2], "2");
    done();
  });

  it("check end_session", function(done) {
    var json = server.shift();
    compare(json, "end_session", device_id);
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
