#!/bin/sh 

gcc videosplitter.c  -o videosplitter -lavcodec -lavformat -lavutil -lswscale -lswresample

