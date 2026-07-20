#!/bin/bash
set -e

show_help() {
  echo "Usage: $0 [OPTIONS] [DIR] [NAME...]"
  echo ""
  echo "Generate Root CA and client certificates for P2P chat."
  echo ""
  echo "Options:"
  echo "  -h, --help     Show this help"
  echo ""
  echo "Arguments:"
  echo "  DIR            Output directory (default: certs)"
  echo "  NAME...        Client names (default: alice bob charlie)"
  echo ""
  echo "Examples:"
  echo "  $0                                # alice, bob, charlie in ./certs"
  echo "  $0 /tmp/keys                      # default names in /tmp/keys"
  echo "  $0 certs alice bob charlie dave   # 4 clients"
  echo "  $0 certs \$(cat users.txt)        # names from file"
  exit 0
}

# Проверка --help
if [ "$1" = "-h" ] || [ "$1" = "--help" ]; then
  show_help
fi

# Парсинг аргументов
if [ $# -eq 0 ]; then
  OUT_DIR="certs"
  NAMES=(client alice bob charlie jack)
elif [ $# -eq 1 ]; then
  OUT_DIR="$1"
  NAMES=(alice bob charlie jack)
else
  OUT_DIR="$1"
  shift
  NAMES=("$@")
fi
mkdir -p "$OUT_DIR"

# Проверяем, существуют ли уже файлы Root CA
if [ -f rootCA.crt ] && [ -f rootCA.key ]; then
  echo "=== Root CA already exists, skipping generation ==="
else
  echo "=== Generating Root CA in $OUT_DIR ==="
  openssl req -x509 -newkey rsa:2048 \
    -keyout "$OUT_DIR/rootCA.key" \
    -out "$OUT_DIR/rootCA.crt" \
    -days 3650 -nodes -subj "/CN=RootCA"
  echo "Root CA: $OUT_DIR/rootCA.key + $OUT_DIR/rootCA.crt"
fi

for name in "${NAMES[@]}"; do
  echo ""
  echo "--- $name ---"

  # Проверяем, существуют ли уже файлы этого пользователя
  if [ -f $OUT_DIR/$name.crt ] && [ -f $OUT_DIR/$name.key ]; then
    echo "$name already exists, skipping"
    continue
  fi

  openssl req -newkey rsa:2048 \
    -keyout "$OUT_DIR/$name.key" \
    -out "$OUT_DIR/$name.csr" \
    -nodes -subj "/CN=$name"

  openssl x509 -req \
    -in "$OUT_DIR/$name.csr" \
    -CA "$OUT_DIR/rootCA.crt" \
    -CAkey "$OUT_DIR/rootCA.key" \
    -CAcreateserial -out "$OUT_DIR/$name.crt" -days 365

  rm $OUT_DIR/$name.csr
  echo "$name: $OUT_DIR/$name.key + $OUT_DIR/$name.crt"
done

rm -f "$OUT_DIR/rootCA.srl"

echo ""
echo "=== Done ==="
ls -1 $OUT_DIR
