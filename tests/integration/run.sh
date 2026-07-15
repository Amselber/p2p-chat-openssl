#!/bin/bash
SESSION="myapp" # имя твоей сессии
LAYOUT="tests/integration/run.kdl"

zellij -n $SESSION --layout "$LAYOUT"
