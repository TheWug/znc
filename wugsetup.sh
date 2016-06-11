#!/bin/bash

./autogen.sh
CXXFLAGS="-ggdb3 --std=c++11 -fPIC" ./configure --enable-run-from-source --datadir=/svc/znc --enable-debug
