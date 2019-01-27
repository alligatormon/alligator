#!/bin/sh

clang --analyze -Xanalyzer -analyzer-output=text -I. */*.c
