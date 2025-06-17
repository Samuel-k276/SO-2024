#ifndef AUXILIAR_H
#define AUXILIAR_H

#include <stddef.h>

char* make_path(const char *directory, const char *filename);
char* make_in_path(const char *directory, const char *filename);
char* make_out_path(const char *directory, const char *filename);
char* make_backup_path(const char *fileout_path, unsigned int backup_number);
char* remove_extension(const char *filename);
char* backup_number_to_char(unsigned int backup_number);
char* get_extension(const char *filename);

#endif // AUXILIAR_H

