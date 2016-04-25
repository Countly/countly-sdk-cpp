"use strict";

var assert = require("assert");

var app_key = "ce894ea797762a11560217117abea9b1e354398c";

module.exports = function(json, c, device_id) {

  if (c === "begin_session") {
    assert.equal(json.app_key, app_key);
    assert.ok(json.device_id.length > 4);
    assert.ok(json.sdk_version.length > 4);
    assert.equal(json.begin_session, "1");
    // TODO // metrics.test(json.metrics);
    return json.device_id;
  } else
  if (c === "keepalive") {
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
    assert.equal(json.session_duration, "30");
  } else
  if (c === "event") {
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
  } else
  if (c === "0") {
    assert.ok(json.timestamp | 0 > 9000);
    assert.equal(json.key, "test0");
    assert.equal(json.count, 15);
  } else
  if (c === "1") {
    assert.ok(json.timestamp | 0 > 9000);
    assert.equal(json.key, "test1");
    assert.equal(json.count, 140);
    assert.equal(json.sum, "12.5");
  } else
  if (c === "2") {
    assert.ok(json.timestamp | 0 > 9000);
    assert.equal(json.key, "test2");
    assert.equal(json.count, 600);
    assert.equal(json.sum, "17");
  } else
  if (c === "end_session") {
    assert.equal(json.app_key, app_key);
    assert.equal(json.device_id, device_id);
    assert.equal(json.end_session, "1");
  }

};
