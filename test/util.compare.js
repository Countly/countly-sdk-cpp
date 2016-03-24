"use strict";

var assert = require("assert");

module.exports = function(json, c, device_id) {

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
  }

};
