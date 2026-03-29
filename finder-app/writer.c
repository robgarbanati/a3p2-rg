#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <syslog.h>

int main(int argc, char *argv[]) {
    openlog("writer", LOG_PID, LOG_USER);

    if (argc != 3) {
        syslog(LOG_ERR, "Error: Expected 2 arguments, got %u\n", argc - 1);
        return 1;
    }

    char *writefilepath = argv[1];
    char *writestr = argv[2];

    int writefile = creat(writefilepath, 0644);
    if (writefile == -1) {
        syslog(LOG_ERR, "Could not open file %s\n", writefilepath);
        return 1;
    }

    ssize_t len = strlen(writestr);
    if (write(writefile, writestr, len) != len) {
        syslog(LOG_ERR, "Could not write %s to file %s\n", writestr, writefilepath);
        close(writefile);
        return 1;
    } else {
        syslog(LOG_DEBUG, "Writing %s to %s\n", writestr, writefilepath);
    }

    close(writefile);
    return 0;
}
