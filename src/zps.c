/**!
 * zps, <TODO: Add description>
 * Copyright (C) 2019 by orhun <https://www.github.com/orhun>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "zps.h"
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <signal.h>
#include <ftw.h>

static int fd,		            /* File descriptor to be used in file operations */
	defunctCount = 0;           /* Number of found defunct processes */
static char *strPath,		    /* String part of a path in '/proc' */
	fileContent[BLOCK_SIZE],    /* Text content of a file */
	buff;                       /* Char variable that used as buffer in read */
typedef struct {	            /* Struct for storing process stats */
	int pid;
	char comm[BLOCK_SIZE/64];
	char state[BLOCK_SIZE/64];
	int ppid;
} ProcStats;
static ProcStats
	defunctProcs[BLOCK_SIZE/4]; /* Array of defunct process' stats */

/*!
 * Read the given file and return its content.
 *
 * @param filename
 * @return fileContent
 */
static char* readFile(char *fileName) {
	/**
	 * Open file with following flags:
	 * O_RDONLY: Open for reading only.
	 * S_IRUSR:  Read permission bit for the owner of the file.
	 * S_IRGRP:  Read permission bit for the group owner of the file.
	 * S_IROTH:  Read permission bit for other users.
	 */
	fd = open(fileName, O_RDONLY, S_IRUSR | S_IRGRP | S_IROTH);
	/* Check for file open error. */
	if (fd == -1)
		return NULL;
	/**
	 * Read bytes from file descriptor into the buffer.
	 * Use 'read until the end' method since it's not always possible to
	 * read file knowing its size. ('/proc' has zero-length virtual files)
	 */
	for (int i = 0; read(fd, &buff, sizeof(buff)) != 0; i++) {
		fileContent[i] = buff;
	}
	/* Close the file descriptor and return file content. */
	close(fd);
	return fileContent;
}

/*!
 * Parse and return the stats from process path.
 *
 * @param procPath   (process path in '/proc')
 * @return procStats (process stats)
 */
static ProcStats getProcStats(const char *procPath) {
	/* Array for storing the stat file name of the process. */
	char pidStatFile[strlen(procPath)+strlen(STAT_FILE)];
	/* Fill the array with the given parameter and append '/stat'. */
	strcpy(pidStatFile, procPath);
	strcat(pidStatFile, STAT_FILE);
	/* Read the PID status file. */
	char *content = readFile(pidStatFile);
	/* Create a structure for storing parsed process' stats. */
	ProcStats procStats;
	/* Parse the '/stat' file into process status struct. */
	sscanf(content, "%d %64s %64s %d", &procStats.pid,
		procStats.comm, procStats.state, &procStats.ppid);
	/* Return the process stats. */
	return procStats;
}

/*!
 * Event for receiving tree entry from '/proc'.
 *
 * @param fpath  (path name of the entry)
 * @param sb     (file status structure for fpath)
 * @param tflag  (type flag of the entry)
 * @param ftwbuf (structure that contains entry base and level)
 * @return EXIT_status
 */
static int procEntryRecv(const char *fpath, const struct stat *sb,
		int tflag, struct FTW *ftwbuf) {
	/**
	 * Check for depth of the fpath (1), type of the entry (directory),
	 * base of the fpath (numeric value) to filter entries except PID.
	 */
    if (ftwbuf->level == 1 && tflag == FTW_D &&
        strtol(fpath + ftwbuf->base, &strPath, 10) &&
        !strcmp(strPath, "")) {
		/* Get the process stats from the path. */
		ProcStats procStats = getProcStats(fpath);
		/* Check for process' file parse error. */
		if (strlen(procStats.state) == 0) {
			fprintf(stderr, "Failed to parse file: '%s'\n", fpath);
			exit(0);
		/* Check for the process state for being zombie. */
		} else if (strstr(procStats.state, STATE_ZOMBIE) != NULL) {
			/* Add process stats to the array of defunct process stats. */
			defunctProcs[defunctCount++] = procStats;
		} else {
			fprintf(stderr, "Process: %d\r", procStats.pid);
		}
    }
    return EXIT_SUCCESS;
}

/*!
 * Check running process' states using the '/proc' filesystem.
 *
 * @return EXIT_status
 */
static int checkProcesses() {
	/**
	 * Call ftw with the following parameters to get '/proc' contents:
	 * PROC_FS:       '/proc' filesystem.
	 * procEntryRecv: Function to call for each entry found in the tree.
	 * USE_FDS:       Maximum number of file descriptors to use.
	 * FTW_PHYS:      Flag for not to follow symbolic links.
	 */
	if (nftw(PROC_FS, procEntryRecv, USE_FDS, FTW_PHYS)) {
		return EXIT_FAILURE;
	}
	/**
	 * Handle the defunct processes after the ftw completes.
	 * Terminating a process while ftw might cause interruption.
	 */
	fprintf(stderr, "%c[2KDefunct (zombie) "
		"processes found: %d\n", 27, defunctCount);
	for(int i = 0; i < defunctCount; i++) {
		fprintf(stderr, "Process (%s): %d, PPID: %d ", defunctProcs[i].state,
					defunctProcs[i].pid, defunctProcs[i].ppid);
		/* Send termination signal to the parent of defunct process. */
		if(!kill(defunctProcs[i].ppid, SIGTERM))
			fprintf(stderr, "(terminated)\n");
		else
			fprintf(stderr, "(failed to terminate)\n");
	}
	return EXIT_SUCCESS;
}

/*!
 * Parse command line arguments.
 *
 * @param argc (argument count)
 * @param argv (argument vector)
 * @return EXIT_status
 */
static int parseArgs(int argc, char **argv){
    int opt;
    while ((opt = getopt(argc, argv, "v")) != -1) {
        switch (opt) {
            case 'v': /* Show version information. */
                fprintf(stderr, "zps v%s\n", VERSION);
                return EXIT_FAILURE;
        }
    }
    return EXIT_SUCCESS;
}

/*!
 * Entry-point
 */
int main(int argc, char *argv[]) {
	/* Parse command line arguments. */
	if(parseArgs(argc, argv))
		return EXIT_SUCCESS;
	/* Check running processes. */
	return checkProcesses();
}