"use strict";

var assert = require("assert");
var http = require("http");
var url = require("url");
var express = require("express");
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

    server = express().get("/i", function(req, res) {
      queue.push(req);
      res.send("Success\n");
    }).listen(port, ip);

  } else
  if (correct === "502") { // 502 Bad Gateway after "countly stop"

    var counter = 0;

    server = express().get("/i", function(req, res) {
      if (counter === 0) {
        counter++;
        res.status(502);
        res.send(
          "<html>\n" +
          "<head><title>502 Bad Gateway</title></head>\n" +
          "<body bgcolor=\"white\">\n" +
          "<center><h1>502 Bad Gateway</h1></center>\n" +
          "<hr><center>nginx/1.4.6 (Ubuntu)</center>\n" +
          "</body>\n" +
          "</html>\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n" +
          "<!-- a padding to disable MSIE and Chrome friendly error page -->\n");
      } else {
        queue.push(req);
        res.send("Success\n");
      }
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

if (!module.parent) {
  module.exports.start(process.argv[2] || "correct");
}
