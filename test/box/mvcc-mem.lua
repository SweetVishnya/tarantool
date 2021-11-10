#!/usr/bin/env tarantool

box.cfg{
    listen                 = os.getenv("LISTEN"),
    memtx_use_mvcc_engine  = true
}

require('console').listen(os.getenv('ADMIN'))