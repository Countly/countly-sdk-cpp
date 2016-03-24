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

  it("wait 5 seconds", function(done) {
    this.timeout(6 * 1000);
    setTimeout(function() {
      done();
    }, 5 * 1000);
  });

  it("start correct server", function(done) {
    server.start("correct");
    done();
  });

  it("wait 5 seconds", function(done) {
    this.timeout(6 * 1000);
    setTimeout(function() {
      done();
    }, 5 * 1000);
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

  it("check end_session", function(done) {
    var json = server.shift();
    compare(json, "end_session", device_id);
    done();
  });

  it("stop server", function(done) {
    server.stop();
    done();
  });

});
