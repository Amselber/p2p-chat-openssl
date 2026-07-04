#!/bin/bash
# certs/cert_info.sh
# Выводит fingerprint и имя из сертификата
# Использование: ./cert_info.sh alice.crt

CERT=$1

# Имя (CN)
echo "name=$(openssl x509 -in $CERT -noout -subject | sed 's/.*CN=//' | sed 's/[,/].*//')"

# Fingerprint (SHA256)
echo "fp=$(openssl x509 -in $CERT | sha256sum | awk '{print $1}')"
