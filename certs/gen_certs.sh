#!/bin/bash
set -e

if [ $# -eq 0 ]; then
  echo "Usage: $0 <name1> [name2] ..."
  echo "Example: $0 alice bob charlie"
  exit 1
fi

# Проверяем, существуют ли уже файлы Root CA
if [ -f rootCA.crt ] && [ -f rootCA.key ]; then
  echo "=== Root CA already exists, skipping generation ==="
else
  echo "=== Generating Root CA ==="
  openssl req -x509 -newkey rsa:2048 -keyout rootCA.key -out rootCA.crt \
    -days 3650 -nodes -subj "/CN=RootCA"
  echo "Root CA: rootCA.key + rootCA.crt"
fi

for name in "$@"; do
  echo ""
  echo "--- $name ---"

  # Проверяем, существуют ли уже файлы этого пользователя
  if [ -f $name.crt ] && [ -f $name.key ]; then
    echo "$name already exists, skipping"
    continue
  fi

  openssl req -newkey rsa:2048 -keyout $name.key -out $name.csr \
    -nodes -subj "/CN=$name"

  openssl x509 -req -in $name.csr -CA rootCA.crt -CAkey rootCA.key \
    -CAcreateserial -out $name.crt -days 365

  rm $name.csr
  echo "$name: $name.key + $name.crt"
done

echo ""
echo "=== Done ==="
ls -1 *.crt *.key
