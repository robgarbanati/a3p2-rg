#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

int main(int argc, char *argv[]) {
    if (argc != 3) {
        printf("Error: Expected 2 arguments, got %u\n", argc - 1);
        return 1;
    }

    char *writefilepath = argv[1];
    char *writestr = argv[2];

    int writefile = creat(writefilepath, 0644);
    if (writefile == -1) {
        printf("Could not open file %s\n", writefilepath);
        return 1;
    }

    ssize_t len = strlen(writestr);
    if (write(writefile, writestr, len) != len) {
        printf("Could not write %s to file %s\n", writestr, writefilepath);
        close(writefile);
        return 1;
    }

    close(writefile);
    return 0;
}
