// This is an HTTP server that listens on port 80 and forwards all requests to
// the the "server" host on port 80.
// It uses Datadog's automatic instrumentation for Node.

const http = require('http');
const process = require('process');

function httpProxy({host, port}) {
    return function (request, response) {
      const options = {
        host,
        port,
        path: request.url,
        method: request.method,
        headers: request.headers
      };

      const upstreamRequest = http.request(options, function (upstreamResponse) {
        response.writeHead(upstreamResponse.statusCode, upstreamResponse.statusMessage, upstreamResponse.headers);
        upstreamResponse.on('data', function (chunk) {
          response.write(chunk);
        }).on('end', function () {
          response.end();
        });
      }).on('error', function (error) {
        console.error('Error occurred while proxying request: ', error);
      });

      request.on('data', function (chunk) {
        upstreamRequest.write(chunk);
      }).on('end', function () {
        upstreamRequest.end();
      });
    };
}

const requestListener = httpProxy({host: 'server', port: 80});

const port = 80;
console.log(`node.js proxy is running on port ${port}`);
const server = http.createServer(requestListener);
server.listen(port);

process.on('SIGTERM', function () {
  server.close(function () { process.exit(0); });
});
