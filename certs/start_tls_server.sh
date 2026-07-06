openssl s_server -cert bob.crt -key bob.key -CAfile rootCA.crt \
  -Verify 1 -accept 44423
