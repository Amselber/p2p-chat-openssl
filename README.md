# p2p-chat-openssl

Децентрализованный чат для локальной сети. В разработке.

## Цель

Общение без сервера. Все узлы равноправны.
Обнаружение через UDP, защита через TLS, идентификация через fingerprint сертификата.

## Статус

- [x] Проектирование архитектуры
- [x] Обнаружение узлов (UDP Multicast)
- [x] TCP-соединения
- [x] TLS-шифрование и проверка сертификатов
- [x] Консольный интерфейс
- [x] Приватные сообщения
- [ ] Передача файлов
- [ ] Тесты и расширенный функционал 

## Быстрый старт 

``` bash
    cd certs && ./gen_certs.sh <name>
    make
    make run


## Запуск на данном этапе

    make
    cd certs && ./gen_certs.sh alice bob
    ./bin/p2pchat -c alice.ini
    ./bin/p2pchat -c bob.ini

    bob: >/_hello
    alice: >/_tls bob
    alice: >/msg bob Hello Bob!

```

## Технологии

Linux, Make, OpenSSL, epoll.
