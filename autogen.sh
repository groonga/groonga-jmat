#!/bin/sh

mkdir -p m4/

${AUTORECONF:-autoreconf} --force --install
