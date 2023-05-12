"""database - a thin wrapper around a sqlite3 database

TODO
"""

import flask
import gevent
from gevent.pywsgi import WSGIServer
import signal
import sqlite3
import sys

DB_FILE_PATH = '/tmp/database.sqlite'

app = flask.Flask(__name__)


@app.route('/')
def hello():
    return "Hello, World!"


@app.route('/query')
def query():
    request = flask.request
    sql = request.args.get('sql')
    if sql is None:
        return ('"sql" query parameter is required.', 400)

    with sqlite3.connect(f'file:{DB_FILE_PATH}?mode=ro') as db:
        try:
            return list(db.execute(sql))
        except Exception as error:
            return str(error) + '\n', 400


@app.route('/execute')
def execute():
    request = flask.request
    sql = request.args.get('sql')
    if sql is None:
        return ('"sql" query parameter is required.', 400)

    with sqlite3.connect(DB_FILE_PATH) as db:
        try:
            db.execute(sql)
            return ''
        except Exception as error:
            return str(error) + '\n', 400


if __name__ == '__main__':
    gevent.signal_handler(signal.SIGTERM, sys.exit)

    with sqlite3.connect(DB_FILE_PATH) as db:
        db.execute('''
        create table if not exists Note(
            AddedWhen text,
            Body text);
        ''')

    http_server = WSGIServer(('', 80), app)
    http_server.serve_forever()
