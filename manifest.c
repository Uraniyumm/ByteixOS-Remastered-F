
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

    if (len == 0 || len > 32)
        return 0;

    if (!((name[0] >= 'a' && name[0] <= 'z') || name[0] == '_'))
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

int ask_superuser(void) {
    char answer[16];

    while (1) {
        if (!read_input(
            "[ByteixOS]: Is this user a super user? (yay or nay)\n> ",
            answer, sizeof(answer)))
            return -1;

        if (strcmp(answer, "yay") == 0)
            return 1;

        if (strcmp(answer, "nay") == 0)
            return 0;

        printf("[ByteixOS]: Are you sure you don't need to go back to kindergarten? That's not an option.\n");
    }
}

void create_user(void) {
    char username[64];

    if (geteuid() != 0) {
        printf("[ByteixOS]: YOU NEED SUDO!!! WHY ARE YOU NOT USING SUDO!!!!!!!!!!!??\n");
        return;
    }

    if (!read_input("[ByteixOS]: New Username: ",
                    username, sizeof(username))) {
        printf("[ByteixOS]: Invalid input.\n");
        return;
    }

    if (!valid_username(username)) {
        printf("[ByteixOS]: Invalid username.\n");
        return;
    }

    if (getpwnam(username) != NULL) {
        printf("[ByteixOS]: HEY THAT USERS ALREADY EXISTS!!!.\n");
        return;
    }

    int superuser = ask_superuser();

    if (superuser < 0)
        return;

    printf("[ByteixOS]: Creating user...\n");

    char *create_args[] = {
        "useradd", "-m", "-s", "/bin/bash", username, NULL
    };

    if (run_command(create_args) != 0) {
        printf("[ByteixOS]: User creation failed.\n");
        return;
    }

    printf("[ByteixOS]: User created. Set their password:\n");

    char *password_args[] = {
        "passwd", username, NULL
    };

    if (run_command(password_args) != 0) {
        printf("[ByteixOS]: Password setup failed.\n");
        printf("[ByteixOS]: The account exists but may not be usable yet.\n");
        return;
    }

    if (superuser) {
        char *admin_args[] = {
            "usermod", "-aG", "wheel", username, NULL
        };

        if (run_command(admin_args) != 0) {
            printf("[ByteixOS]: Could not grant wheel membership.\n");
            return;
        }

        printf("[ByteixOS]: Superuser group assigned.\n");
    }

    struct passwd *user_info = getpwnam(username);

    if (user_info == NULL) {
        printf("[ByteixOS]: Could not retrieve user information.\n");
        return;
    }

    printf("\n[ByteixOS]: USER ACCOUNT OVERVIEW\n");
    printf("--------------------------------\n");
    printf("Username: %s\n", user_info->pw_name);
    printf("User ID: %u\n", (unsigned int)user_info->pw_uid);
    printf("Home: %s\n", user_info->pw_dir);
    printf("Shell: %s\n", user_info->pw_shell);
    printf("Superuser: %s\n", superuser ? "yay" : "nay");
    printf("--------------------------------\n");

    printf("[ByteixOS]: User created.\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: manifest <object>\n");
        return 1;
    }

    if (strcmp(argv[1], "user") == 0) {
        create_user();
    }
    else if (strcmp(argv[1], "group") == 0) {
        printf("[ByteixOS]: Group creation is not implemented yet.\n");
    }
    else {
        printf("[ByteixOS]: Unknown object type: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
