// assign.c is for ByteixOS-Remastered
/* assign.c holds the logic and code for assigning groups or individual users access to directories
or files */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <termios.h>
#include <sys/wait.h>
#include <fcntl.h>

#define USERNAME_MAX_LEN 32
#define GROUPNAME_MAX_LEN 32

int run_command(char *args[]) {
    pid_t pid = fork();

    if (pid < 0) {
        perror("fork");
        return -1;
    }

    if (pid == 0) {
        execvp(args[0], args);
        perror("execvp");
        _exit(127);
    }

    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) {
            perror("waitpid");
            return -1;
        }
    }

    if (WIFEXITED(status))
        return WEXITSTATUS(status);

    return -1;
}

int read_input(const char *prompt, char *buffer, size_t size) {
    printf("%s", prompt);
    fflush(stdout);

    if (fgets(buffer, size, stdin) == NULL)
        return 0;

    if (strchr(buffer, '\n') == NULL && !feof(stdin)) {
        int c;
        while ((c = getchar()) != '\n' && c != EOF);
        return 0;
    }

    buffer[strcspn(buffer, "\n")] = '\0';

    return 1;
}

int valid_username(const char *name) {
    size_t len = strlen(name);

    if (len == 0 || len > USERNAME_MAX_LEN)
        return 0;

    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          name[0] == '_'))
        return 0;

    for (size_t i = 1; i < len; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }

    return 1;
}

int valid_groupname(const char *name) {
    size_t len = strlen(name);

    if (len == 0 || len > GROUPNAME_MAX_LEN)
        return 0;

    if (!((name[0] >= 'a' && name[0] <= 'z') ||
          name[0] == '_'))
        return 0;

    for (size_t i = 1; i < len; i++) {
        char c = name[i];

        if (!((c >= 'a' && c <= 'z') ||
              (c >= '0' && c <= '9') ||
              c == '_' || c == '-'))
            return 0;
    }

    return 1;
}


int identify_target(const char *input) {
    if (input == NULL || input[0] == '\0' || input[1] == '\0')
        return 0;

    const char *name = input + 1;

    // ; identifies an individual user
    if (input[0] == ';') {
        if (!valid_username(name))
            return 0;

        struct passwd *user = getpwnam(name);

        if (user == NULL)
            return 0;

        return 1;
    }

    // : identifies a user group
    if (input[0] == ':') {
        if (!valid_groupname(name))
            return 0;

        struct group *group = getgrnam(name);

        if (group == NULL)
            return 0;

        return 2;
    }

    return 0;
}


int ask_confirmation(void) {
    char answer[16];

    while (1) {
        if (!read_input(
            "[ByteixOS]: Is this correct? (yay or nay)\n> ",
            answer, sizeof(answer)))
            return -1;

        if (strcmp(answer, "yay") == 0)
            return 1;

        if (strcmp(answer, "nay") == 0)
            return 0;

        printf("[ByteixOS]: Are you sure you don't need to go back to kindergarten? That's not an option.\n");
    }
}

void assign_user_to_file(const char *username, const char *filepath) {
    char path_to_dir_or_file[4096];

    if (geteuid() != 0) {
        printf("[ByteixOS]: YOU NEED SUDO!!! WHY ARE YOU NOT USING SUDO!!!!!!!!!!!??\n");
        return;
    }

    printf("[ByteixOS]: Please enter the valid path to the directory or file you want to assign.\n> ");

    if (!read_input("", path_to_dir_or_file, sizeof(path_to_dir_or_file))) {
        printf("[ByteixOS]: Invalid or fat path.\n");
        return;
    }

    if (path_to_dir_or_file[0] == '\0') {
        printf("[ByteixOS]: No path provided.\n");
        return;
    }

    struct stat path_info;

    if (stat(path_to_dir_or_file, &path_info) != 0) {
        perror("[ByteixOS]: Path");
        return;
    }

    printf("[ByteixOS]: Path validated: %s\n", path_to_dir_or_file);
}