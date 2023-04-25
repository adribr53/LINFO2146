#!/bin/bash

for filename in project/*; do
    cp -r $filename $CNG_PATH/examples/
done
