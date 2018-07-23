#!/bin/bash -ex

for f in fileserver makedirs mongo networkfs oltp openfiles \
		    removedirs varmail \
		    videoserver webproxy webserver
do
    echo $f
    echo "load $f
run 120" | filebench
done

echo DONE
