
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

        printf("[ByteixOS]: That's not an option. Try again.\n");
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
        printf("[ByteixOS]: HEY THAT USER ALREADY EXISTS!!!.\n");
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
        printf("[ByteixOS]: PASSWORD SETUP FAILED!!!!!.\n");
        printf("[ByteixOS]: THIS ACCOUNT EXISTS BUT MAY NOT BE USABLE YET..!\n");
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

int create_group(void) {
    char groupname[64];
    char users[256];
    char users_check[256];
    char users_add[256];

    if (geteuid() != 0) {
        printf("[ByteixOS]: YOU NEED SUDO!!! WHY ARE YOU NOT USING SUDO!!!!!!!!!!!??\n");
        return 1;
    }

    // Request group name
    if (!read_input(
        "[ByteixOS]: What would you like to name this group?\n> ",
        groupname, sizeof(groupname))) {
        printf("[ByteixOS]: Invalid input.\n");
        return 1;
    }

    if (!valid_username(groupname)) {
        printf("[ByteixOS]: Invalid group name.\n");
        return 1;
    }

    if (getgrnam(groupname) != NULL) {
        printf("[ByteixOS]: THAT GROUP ALREADY EXISTS!!!\n");
        return 1;
    }

    // Request group members
    if (!read_input(
        "[ByteixOS]: What users are in this group?\n> ",
        users, sizeof(users))) {
        printf("[ByteixOS]: Invalid input.\n");
        return 1;
    }

    // Preserve the original input for display
    strcpy(users_check, users);
    strcpy(users_add, users);

    // Validate all users before creating the group
    char *user_list = strtok(users_check, " \t");

    if (user_list == NULL) {
        printf("[ByteixOS]: You need to specify at least one user.\n");
        return 1;
    }

    while (user_list != NULL) {
        if (!valid_username(user_list)) {
            printf("[ByteixOS]: Invalid username: %s\n", user_list);
            return 1;
        }

        if (getpwnam(user_list) == NULL) {
            printf("[ByteixOS]: User %s does not exist.\n", user_list);
            return 1;
        }

        user_list = strtok(NULL, " \t");
    }

    // Group creation overview
    printf("\n[ByteixOS]: GROUP CREATION OVERVIEW\n");
    printf("--------------------------------\n");
    printf("Group Name: %s\n", groupname);
    printf("Users: %s\n", users);
    printf("--------------------------------\n");

    int confirmation = ask_confirmation();

    if (confirmation < 0)
        return 1;

    if (!confirmation) {
        printf("[ByteixOS]: Group creation canceled.\n");
        return 0;
    }

    // Create group
    printf("[ByteixOS]: Creating group...\n");

    char *group_args[] = {
        "groupadd", groupname, NULL
    };

    if (run_command(group_args) != 0) {
        printf("[ByteixOS]: Group creation failed.\n");
        return 1;
    }

    // Assign users to group
    user_list = strtok(users_add, " \t");

    while (user_list != NULL) {
        char *usermod_args[] = {
            "usermod", "-aG", groupname, user_list, NULL
        };

        if (run_command(usermod_args) != 0) {
            printf("[ByteixOS]: Could not add user %s to group %s.\n",
                   user_list, groupname);

            printf("[ByteixOS]: Group exists, but membership setup may be incomplete.\n");
            return 1;
        }

        user_list = strtok(NULL, " \t");
    }

    printf("\n[ByteixOS]: GROUP CREATION OVERVIEW\n");
    printf("--------------------------------\n");
    printf("Group Name: %s\n", groupname);
    printf("Users: %s\n", users);
    printf("--------------------------------\n");

    printf("[ByteixOS]: Group created and users assigned successfully.\n");

    return 0;
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
        return create_group();
    }
    else {
        printf("[ByteixOS]: Unknown object type: %s\n", argv[1]);
        return 1;
    }

    return 0;
}
