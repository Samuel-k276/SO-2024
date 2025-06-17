#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>

#include "auxiliar.h"

char* make_path(const char *directory, const char *filename) {
  char *path = (char*)malloc(strlen(directory) + strlen(filename) + 2);
  if (path == NULL) {
    return NULL;
  }

  strcpy(path, directory);
  strcat(path, "/");
  strcat(path, filename);

  return path;
}

char* make_in_path(const char *directory, const char *filename) {
  char *path = (char*)malloc((strlen(directory) + strlen(filename) + 2)*sizeof(char));
    if (path == NULL) {
      fprintf(stderr, "Failed to allocate memory for backup path\n");
    return NULL;
  }

  strcpy(path, directory);
  strcat(path, "/");
  strcat(path,filename);
  return path;
}

char* make_out_path(const char *directory, const char *filename) {
  char *path = (char*)malloc((strlen(directory) + strlen(filename) + 2)*sizeof(char));
  char *filename_ = remove_extension(filename);
    if (path == NULL) {
      fprintf(stderr, "Failed to allocate memory for backup path\n");
    return NULL;
  }
  
  strcpy(path,directory);
  strcat(path, "/");
  strcat(path, filename_);
  free(filename_);
  strcat(path,".out");
  return path;
}

char* make_backup_path(const char *fileout_path, unsigned int backup_number) {
  char *backup_number_str = backup_number_to_char(backup_number);
  char *filename_ = remove_extension(fileout_path);

  char *path = (char*)malloc((strlen(filename_) + strlen(backup_number_str) + 6)*sizeof(char));
  if (path == NULL) {
    fprintf(stderr, "Failed to allocate memory for backup path\n");
    return NULL;
  }
  strcpy(path, filename_);
  strcat(path, "-");
  strcat(path, backup_number_str);
  strcat(path, ".bck");
  free(backup_number_str);

  return path;
}

char* remove_extension(const char *filename) {
  char *extension = strrchr(filename, '.');
  if (extension == NULL) {
    return strdup(filename);
  }

  char *name = (char*)malloc(strlen(filename) - strlen(extension) + 1);
  if (name == NULL) {
    return NULL;
  }

  strncpy(name, filename, strlen(filename) - strlen(extension));
  
  name[extension - filename] = '\0';

  return name;
  
}

char* get_extension(const char *filename) {
  char *extension = strrchr(filename, '.');
  if (extension == NULL) {
    return NULL;
  }

  return extension;
}

char* backup_number_to_char(unsigned int backup_number) {
  unsigned int backup_number_str_len = 0;
  unsigned int backup_number_ = backup_number;
  while (backup_number > 0) {
    backup_number /= 10;
    backup_number_str_len++;
  }
  char *backup_number_str = (char*)malloc((backup_number_str_len + 1)*sizeof(char));
  if (backup_number_str == NULL) {
    return NULL;
  }
  unsigned int last = backup_number_str_len;
  backup_number = backup_number_;
  while (backup_number > 0) {
    backup_number_str_len--;
    backup_number_str[backup_number_str_len] = (char)(backup_number % 10) + '0';
    backup_number /= 10;
  }
  backup_number_str[last] = '\0';
      
  return backup_number_str;
}