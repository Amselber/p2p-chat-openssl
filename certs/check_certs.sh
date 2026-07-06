# Проверить, что сертификаты подписаны одним rootCA
openssl verify -CAfile certs/rootCA.crt certs/client.crt
openssl verify -CAfile certs/rootCA.crt certs/alice.crt
openssl verify -CAfile certs/rootCA.crt certs/bob.crt

# Проверить, что CN совпадает с ожидаемым
openssl x509 -in certs/client.crt -noout -subject
openssl x509 -in certs/alice.crt -noout -subject
openssl x509 -in certs/bob.crt -noout -subject
