"use strict";

var assert = require("assert");

module.exports.test = function(event, number) {

  if (number === 0) {
    assert.ok(event.timestamp | 0 > 9000);
    assert.equal(event.key, "test0");
    assert.equal(event.count, 15);
  } else
  if (number === 1) {
    assert.ok(event.timestamp | 0 > 9000);
    assert.equal(event.key, "test1");
    assert.equal(event.count, 140);
    assert.equal(event.sum, "12.5");
  } else
  if (number === 2) {
    assert.ok(event.timestamp | 0 > 9000);
    assert.equal(event.key, "test2");
    assert.equal(event.count, 600);
    assert.equal(event.sum, "17");
  }

};
