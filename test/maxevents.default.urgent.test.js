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

  it("make 53 events", function(done) {
    binary.command(
      "0122101221" + "0122101221" +
      "0122101221" + "0122101221" +
      "0122101221" + "012");
    done();
  });

  it("wait 1.5 seconds", function(done) {
    this.timeout(2 * 1000);
    setTimeout(done, 1.5 * 1000);
  });

  it("binary should exit", function(done) {
    binary.stop(done);
  });

  it("stop server", function(done) {
    server.stop();
    done();
  });

  var device_id;

  it("check begin_session", function(done) {
    var json = server.shift();
    device_id = compare(json, "begin_session");
    done();
  });

  it("check 50 events pack", function(done) {
    var json = server.shift();
    compare(json, "event", device_id);
    assert.equal(json.events.length, 50);
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

});
