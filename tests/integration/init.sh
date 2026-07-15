#!/bin/bash

DIRS=(
  "chat1"
  "chat2"
  "chat3"
  "chat4"
)

for dir in "${DIRS[@]}"; do
  if [ -d "$dir" ]; then
    echo "exist: $dir"
  else
    mkdir -p bin/test/"$dir"/{downloads,certs}
  fi
done

echo "Test file for send. Hello World!" >bin/test/file.txt
cp bin/test/file.txt bin/test/chat1/downloads

certs/gen_certs.sh alice bob jack johnny

for dir in "${DIRS[@]}"; do
  cp bin/p2pchat bin/test/"$dir"
  cp rootCA.crt bin/test/"$dir"/certs/
done

mv alice.key bin/test/chat1/certs/client.key
mv alice.crt bin/test/chat1/certs/client.crt
mv bob.key bin/test/chat2/certs/client.key
mv bob.crt bin/test/chat2/certs/client.crt
mv jack.key bin/test/chat3/certs/client.key
mv jack.crt bin/test/chat3/certs/client.crt
mv johnny.key bin/test/chat4/certs/client.key
mv johnny.crt bin/test/chat4/certs/client.crt
mv rootCA.* bin/test
