#!/bin/bash
set -e

docker build -t liwus-builder .
docker run --rm -v "$(pwd)":/os-build liwus-builder make all
