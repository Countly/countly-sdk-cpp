"use strict";

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
  return json;
};

var ip = module.exports.ip = "127.0.0.1";
var port = module.exports.port = 1337;

http.createServer(function(req, res) {
  queue.push(req);
}).listen(port, ip);
