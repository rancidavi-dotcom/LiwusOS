#!/bin/bash
docker run --rm -v "$(pwd)":/os-build liwus-builder make clean
