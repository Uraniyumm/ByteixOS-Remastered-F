// assign.c 

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pwd.h>
#include <grp.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>
#include <sys/wait.h>

#define USERNAME_MAX_LEN 32
#define GROUPNAME_MAX_LEN 32
#define PATH_BUFFER_SIZE 4096
#define INPUT_BUFFER_SIZE 4096
#define MAX_TARGETS 64
#define ACL_BUFFER_SIZE 8192

typedef struct {
    char type;
    char name[GROUPNAME_MAX_LEN + 1];
} Assignment;

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

/* ; = user
   : = group
   0 = invalid
   1 = user
   2 = group
*/

int identify_target(const char *input) {
    if (input == NULL || input[0] == '\0' || input[1] == '\0')
        return 0;

    const char *name = input + 1;

    if (input[0] == ';') {
        if (!valid_username(name))
            return 0;

        return getpwnam(name) != NULL ? 1 : 0;
    }

    if (input[0] == ':') {
        if (!valid_groupname(name))
            return 0;

        return getgrnam(name) != NULL ? 2 : 0;
    }

    return 0;
}

int ask_confirmation(void) {
    char answer[16];

    while (1) {
        if (!read_input(
            "[ByteixOS]: Is this correct? (yay or nay)\n> ",
            answer,
            sizeof(answer)
        ))
            return -1;

        if (strcmp(answer, "yay") == 0)
            return 1;

        if (strcmp(answer, "nay") == 0)
            return 0;

        printf("[ByteixOS]: Are you sure you don't need to go back to kindergarten? That's not an option.\n");
    }
}

int append_acl(char *acl, size_t size, const char *entry) {
    size_t used = strlen(acl);

    if (used >= size - 1)
        return 0;

    int written = snprintf(
        acl + used,
        size - used,
        "%s%s",
        used ? "," : "",
        entry
    );

    return written > 0 && (size_t)written < size - used;
}

int collect_targets(Assignment targets[], int *count) {
    char input[INPUT_BUFFER_SIZE];

    if (!read_input(
        "[ByteixOS]: Who would you like to give access to?\n> ",
        input,
        sizeof(input)
    )) {
        printf("[ByteixOS]: Invalid or fat input.\n");
        return 0;
    }

    *count = 0;

    char *saveptr;
    char *token = strtok_r(input, " \t", &saveptr);

    while (token != NULL) {
        if (*count >= MAX_TARGETS) {
            printf("[ByteixOS]: Too many assignment targets.\n");
            return 0;
        }

        int type = identify_target(token);

        if (type == 0) {
            printf("[ByteixOS]: Invalid or nonexistent target: %s\n", token);
            return 0;
        }

        Assignment *target = &targets[*count];

        target->type = type == 1 ? 'u' : 'g';

        snprintf(
            target->name,
            sizeof(target->name),
            "%s",
            token + 1
        );

        for (int i = 0; i < *count; i++) {
            if (targets[i].type == target->type &&
                strcmp(targets[i].name, target->name) == 0) {
                printf("[ByteixOS]: Duplicate assignment target: %s\n", token);
                return 0;
            }
        }

        (*count)++;

        token = strtok_r(NULL, " \t", &saveptr);
    }

    if (*count == 0) {
        printf("[ByteixOS]: No assignment targets provided.\n");
        return 0;
    }

    return 1;
}

void show_overview(
    const char *filepath,
    Assignment targets[],
    int count
) {
    printf("\n[ByteixOS]: ASSIGNMENT OVERVIEW:\n");
    printf("Path: %s\n", filepath);
    printf("Assigned: ");

    for (int i = 0; i < count; i++) {
        printf(
            "%c%s",
            targets[i].type == 'u' ? ';' : ':',
            targets[i].name
        );

        if (i < count - 1)
            printf(", ");
    }

    printf("\n");
}

int assign_user_to_file(
    Assignment targets[],
    int count,
    const char *filepath,
    const struct stat *path_info
) {
    char acl[ACL_BUFFER_SIZE] = "";

    int is_directory = S_ISDIR(path_info->st_mode);

    // access_mode is set to "rwx" for directories or executable files, otherwise "rw-"
    const char *access_mode = "rw-";

    if (is_directory ||
        (path_info->st_mode &
         (S_IXUSR | S_IXGRP | S_IXOTH))) {
        access_mode = "rwx";
    }

    for (int i = 0; i < count; i++) {
        char entry[128];

        snprintf(
            entry,
            sizeof(entry),
            "%c:%s:%s",
            targets[i].type,
            targets[i].name,
            access_mode
        );

        if (!append_acl(acl, sizeof(acl), entry)) {
            printf("[ByteixOS]: ACL specification is too large.\n");
            return 0;
        }

        if (is_directory) {
            snprintf(
                entry,
                sizeof(entry),
                "d:%c:%s:%s",
                targets[i].type,
                targets[i].name,
                access_mode
            );

            if (!append_acl(acl, sizeof(acl), entry)) {
                printf("[ByteixOS]: ACL specification is too large.\n");
                return 0;
            }
        }
    }

    char *args[] = {
        "setfacl",
        "-m",
        acl,
        "--",
        (char *)filepath,
        NULL
    };

    if (run_command(args) != 0) {
        printf("[ByteixOS]: Failed to create access point.\n");
        return 0;
    }

    return 1;
}

int main(int argc, char *argv[]) {
    if (geteuid() != 0) {
        printf("[ByteixOS]: Error: This operation requires superuser (sudo) privileges.\n");
        printf("[ByteixOS]: YOU NEED SUDO!!! WHY ARE YOU NOT USING SUDO!!!!!!!!!!!??\n");
        return 1;
    }

    if (argc != 2) {
        printf("[ByteixOS]: Usage: sudo assign <path>\n");
        return 1;
    }

    const char *filepath = argv[1];

    if (strlen(filepath) >= PATH_BUFFER_SIZE) {
        printf("[ByteixOS]: Invalid or fat path.\n");
        return 1;
    }

    struct stat path_info;

    if (stat(filepath, &path_info) != 0) {
        printf("[ByteixOS]: Path: ");
        perror(NULL);
        return 1;
    }

    if (!S_ISDIR(path_info.st_mode) &&
        !S_ISREG(path_info.st_mode)) {
        printf("[ByteixOS]: Unsupported file type.\n");
        return 1;
    }

    Assignment targets[MAX_TARGETS];
    int count = 0;

    if (!collect_targets(targets, &count))
        return 1;

    show_overview(filepath, targets, count);

    int confirmation = ask_confirmation();

    if (confirmation == 0) {
        printf("[ByteixOS]: Assignment cancelled.\n");
        return 0;
    }

    if (confirmation < 0) {
        printf("[ByteixOS]: Input interrupted.\n");
        return 1;
    }

    if (!assign_user_to_file(
        targets,
        count,
        filepath,
        &path_info
    )) {
        return 1;
    }

    printf("[ByteixOS]: Access point created.\n");

    return 0;
}
