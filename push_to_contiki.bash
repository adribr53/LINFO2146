#!/bin/bash

for filename in project/*; do
    rm -r $CNG_PATH/examples/$(basename $filename)
    cp -r $filename $CNG_PATH/examples/
done

for filename in simu/*; do
    rm -r $CNG_PATH/$(basename $filename)
    cp -r $filename $CNG_PATH/
done
