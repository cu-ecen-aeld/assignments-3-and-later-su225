#include <stdio.h>
#include <syslog.h>
#include <stddef.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

// get_directory returns the directory for a given file. It is the
// equivalent of dirname() command. It allocates memory and it is the
// responsibility of the caller to free it.
char *get_directory(const char *file) {
  int length = strlen(file);
  char *filepath_copy = (char *)malloc((length+1) * sizeof(char));
  strcpy(filepath_copy, file);
  char *directory = dirname(filepath_copy);
  return directory;
}

int main(int argc, char* argv[]) {
  openlog(NULL, 0, LOG_USER);
  if (argc != 3) {
    syslog(LOG_ERR, "Invalid number of arguments: %d", argc-1);
    return 1;
  }
  const char *writefile = argv[1];
  const char *writestr = argv[2];

  // Check if the directory exists or create one if it does not
  char *dir = get_directory(writefile);
  struct stat st;
  int ret = stat(dir, &st);
  if (ret != 0 || !S_ISDIR(st.st_mode)) {
    if (mkdir(dir, 0777) != 0) {
      syslog(LOG_ERR, "Failed to create directory %s: %s", dir, strerror(errno));
      free(dir);
      return 1;
    }
  }
  free(dir);

  // Now create the file required and write the string
  FILE *file = fopen(writefile, "w");
  if (file == NULL) {
    syslog(LOG_ERR, "Failed to open the file: %s", strerror(errno));
    return 1;
  }
  syslog(LOG_DEBUG, "Writing %s to %s", writestr, writefile);
  int res = fputs(writestr, file);
  if (res < 0) {
    syslog(LOG_ERR, "Failed to write to the file: %s", strerror(errno));
    fclose(file);
    return 1;
  }
  fclose(file);


  return 0;
}
