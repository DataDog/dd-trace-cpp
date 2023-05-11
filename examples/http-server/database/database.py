from flask import Flask
import gevent
from gevent.pywsgi import WSGIServer
import signal
import sys

app = Flask(__name__)

@app.route("/")
def hello():
    return "Hello, World!"

if __name__ == '__main__':
    gevent.signal_handler(signal.SIGTERM, sys.exit)
    http_server = WSGIServer(('', 80), app)
    http_server.serve_forever()
