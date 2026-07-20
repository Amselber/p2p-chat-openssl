#ifndef FILE_TRANSFER_H
#define FILE_TRANSFER_H

/* Обработать заголовок FILE:... и начать приём */
void file_transfer_start(int fd, const char *header);

/* Продолжить приём данных */
void file_transfer_receive(int fd);

/* Отменить передачу (при отключении пира) */
void file_transfer_cleanup(int fd);

/* Отправить файл */
int file_transfer_send(int fd, const char *path);

#endif
