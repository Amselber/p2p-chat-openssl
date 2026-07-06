openssl s_client -cert alice.crt -key alice.key -CAfile rootCA.crt \
  -connect 127.0.0.1:44423
