"use strict";

const fs = require("fs");
const path = require("path");
const assert = require("assert");
const mocha = require("mocha");
const describe = mocha.describe;
const it = mocha.it;

describe(path.basename(__filename), function() {

  it("...", function(done) {
    done();
  });

});
