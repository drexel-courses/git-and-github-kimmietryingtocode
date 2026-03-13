#define _GNU_SOURCE
#include <sched.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mount.h>
#include <sys/wait.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>

#define STACK_SIZE (1024 * 1024)
char child_stack[STACK_SIZE];

// --- Configuration Constants ---
const char *new_hostname = "cs503-container";
const char *container_rootfs = "./nginx-rootfs";  // Must contain a working nginx install
const char *container_cmd[] = { "/usr/sbin/nginx", "-g", "daemon off;", NULL };

// --- Provided: Sets the container's hostname using sethostname() ---
void setup_hostname() {
    if (sethostname(new_hostname, strlen(new_hostname)) == -1) {
        perror("sethostname failed");
        exit(1);
    }
}

// --- TODO: Mount procfs inside the container's root filesystem ---
void setup_mounts() {
    if (mkdir("/proc", 0555) == -1 && errno != EEXIST) {
        perror("mkdir /proc failed");
        exit(1);
    }

    if (mount("proc", "/proc", "proc", 0, "") == -1) {
        perror("mount /proc failed");
        exit(1);
    }
}

// --- TODO: Use chroot() to change root filesystem to container_rootfs ---
void setup_rootfs() {
    if (chdir(container_rootfs) == -1) {
        perror("chdir rootfs failed");
        exit(1);
    }

    if (chroot(".") == -1) {
        perror("chroot failed");
        exit(1);
    }

    if (chdir("/") == -1) {
        perror("chdir / failed");
        exit(1);
    }
}

// --- Provided: This is the child process entrypoint ---
int child_fn(void *arg) {
    (void)arg;
    printf("Child [%d] - inside the container\n", getpid());

    if (mount(NULL, "/", NULL, MS_REC | MS_PRIVATE, NULL) == -1) {
        perror("mount propagation setup failed");
        return 1;
    }

    setup_hostname();
    setup_rootfs();    // TODO: Implement this
    setup_mounts();    // TODO: Implement this

    printf("Launching nginx...\n");
    execvp(container_cmd[0], (char *const *)container_cmd);
    perror("execvp failed");
    return 1;
}

// --- Provided: This is the parent process ---
int main() {
    printf("Parent [%d] - launching container\n", getpid());

    int flags = CLONE_NEWUTS | CLONE_NEWPID | CLONE_NEWNS | SIGCHLD;
    pid_t child_pid = clone(child_fn, child_stack + STACK_SIZE, flags, NULL);

    if (child_pid == -1) {
        perror("clone failed");
        exit(1);
    }

    waitpid(child_pid, NULL, 0);
    printf("Parent - container stopped\n");
    return 0;
}
