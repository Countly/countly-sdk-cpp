"use strict";

var assert = require("assert");
var http = require("http");
var url = require("url");
var qs = require("querystring");
var queue = [];

module.exports.shift = function() {
  var req = queue.shift();
  if (!req) return;
  var query = url.parse(req.url).query;
  var json = qs.parse(query);
  if (json.metrics) json.metrics = JSON.parse(json.metrics);
  if (json.events) json.events = JSON.parse(json.events);
  return json;
};

var ip = module.exports.ip = "127.0.0.1";
var port = module.exports.port = 1337;
var server;

module.exports.start = function(correct) {

  assert(!server);

  if (correct === "correct") {

    server = http.createServer(function(req, res) {
      queue.push(req);
      res.writeHead(200, { "Content-Type": "text/plain" });
      res.end("Success\n");
    }).listen(port, ip);

  } else {
    assert(false);
  }

};

module.exports.stop = function() {
  assert(server);
  server.close();
  server = null;
}
