#!/bin/sh
find . -type f -print0 | xargs -0 dos2unix -ic0 | xargs -0 dos2unix -b
