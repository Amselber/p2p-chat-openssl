// src/version.h
#ifndef VERSION_H
#define VERSION_H

#define VERSION_MAJOR 0
#define VERSION_MINOR 3
#define VERSION_PATCH 0

// Макрос-строка, чтобы не дёргать три числа по отдельности
#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)
#define VERSION_STRING                                                         \
  STR(VERSION_MAJOR) "." STR(VERSION_MINOR) "." STR(VERSION_PATCH)

#endif
