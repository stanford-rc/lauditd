/*
 * lauditd - Lustre Changelogs Daemon
 *
 * Copyright (C) 2020 Stephane Thiell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
 */


#ifdef HAVE_CONFIG_H
#include "lauditd_config.h"
#endif

#include <errno.h>
#include <assert.h>
#include <libgen.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>
#include <signal.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <search.h>

#include <linux/lnet/nidstr.h>
#include <lustre/lustreapi.h>


#ifndef LPX64
# define LPX64   "%#llx"
#endif

#define IGNORE_TYPES_H_MAX 30               /* > max number of changelogs types */

enum lauditd_enqueue_status {
    LAUDITD_ENQUEUE_SUCCESS = 0,            /* read/write success and more to process */
    LAUDITD_ENQUEUE_READER_FAILURE = 1,     /* changelog reader failure (or EOF) */
    LAUDITD_ENQUEUE_WRITER_FAILURE = 2      /* fifo writer failure (or EOF) */
};

static int TerminateSig;

static void usage(void)
{
    fprintf(stderr, "Usage: lauditd [-u cl1] [-f fifo_path] [-b batch_size] [-i ignore_types] <mdtname>\n");
}

static void lauditd_sigterm(int signo)
{
    TerminateSig = 1;
}

static void lauditd_cleanup(const char *fifopath)
{
    int rc;

    rc = unlink(fifopath);
    if (!rc) {
        fprintf(stderr, "successfully removed FIFO %s\n", fifopath);
    } else {
        fprintf(stderr, "cannot remove FIFO %s (%s)\n", fifopath, strerror(errno));
    }
}

static int lauditd_openfifo(const char *fifopath)
{
    int wfd;

    // Open FIFO for write only 
    wfd  = open(fifopath, O_WRONLY); 
    if (wfd < 0) {
        if (errno == EINTR && TerminateSig) {
            exit(EXIT_SUCCESS);
        } else {
            fprintf(stderr, "FATAL: fifo file %s cannot be opened (%s)\n", fifopath,
                    strerror(errno));
            exit(EXIT_FAILURE);
        }
    }
    fprintf(stderr, "fifo successfully connected to %s (fd=%d)\n", fifopath, wfd);
    return wfd;
}

static int lauditd_writerec(int wfd, const char *device, struct changelog_rec *rec)
{
    int         rc;
    time_t      secs;
    struct tm   ts;
    char        buf[32];
    char        tzbuf[8];
    char       *linebufptr;
    int         linebuflen;
    char        linebuf[4096];

    linebufptr = linebuf;
    linebuflen = sizeof(linebuf);

    secs = rec->cr_time >> 30;
    localtime_r(&secs, &ts);

    strftime(buf, sizeof(buf), "%FT%T", &ts);
    strftime(tzbuf, sizeof(tzbuf), "%z", &ts);

    rc = snprintf(linebufptr, linebuflen, "%s.%06d%s mdt=%s id=%llu type=%-5s flags=0x%x",
                 buf, (int)(rec->cr_time & ((1 << 30) - 1)), tzbuf,
                 device, rec->cr_index, changelog_type2str(rec->cr_type),
                 rec->cr_flags & CLF_FLAGMASK);
    if (rc < 0 || rc >= linebuflen)
        goto error;

    linebufptr += rc;
    linebuflen -= rc;

    if (rec->cr_flags & CLF_EXTRA_FLAGS) {
        struct changelog_ext_uidgid *uidgid = changelog_rec_uidgid(rec);;
        rc = snprintf(linebufptr, linebuflen, " uid=%llu gid=%llu",
                      uidgid->cr_uid, uidgid->cr_gid);
        if (rc < 0 || rc >= linebuflen)
            goto error;
        linebufptr += rc;
        linebuflen -= rc;
    }

    if (rec->cr_flags & CLF_JOBID) {
        const char *jobid = (const char *)changelog_rec_jobid(rec);
        if (*jobid) {
            rc = snprintf(linebufptr, linebuflen, " jobid=%s", jobid);
            if (rc < 0 || rc >= linebuflen)
                goto error;
            linebufptr += rc;
            linebuflen -= rc;
        }
    }

    if (rec->cr_flags & CLFE_NID) {
        struct changelog_ext_nid *nid = changelog_rec_nid(rec); 
        libcfs_nid2str_r(nid->cr_nid, buf, sizeof(buf));
        rc = snprintf(linebufptr, linebuflen, " nid=%s", buf);
        if (rc < 0 || rc >= linebuflen)
            goto error;
        linebufptr += rc;
        linebuflen -= rc;
    }

    rc = snprintf(linebufptr, linebuflen, " target="DFID, PFID(&rec->cr_tfid));
    if (rc < 0 || rc >= linebuflen)
        goto error;
    linebufptr += rc;
    linebuflen -= rc;

    if (rec->cr_flags & CLF_RENAME) {
        struct changelog_ext_rename *rnm;

        rnm = changelog_rec_rename(rec);
        if (!fid_is_zero(&rnm->cr_sfid)) {
            rc = snprintf(linebufptr, linebuflen,
                          " source="DFID" source_parent="DFID" source_name=\"%.*s\"",
                         PFID(&rnm->cr_sfid), PFID(&rnm->cr_spfid),
                         changelog_rec_snamelen(rec), changelog_rec_sname(rec));
            if (rc < 0 || rc >= linebuflen)
                goto error;
            linebufptr += rc;
            linebuflen -= rc;
        }
    }

    if (rec->cr_namelen) {
        rc = snprintf(linebufptr, linebuflen, " parent="DFID" name=\"%.*s\"",
                      PFID(&rec->cr_pfid), rec->cr_namelen, changelog_rec_name(rec));
        if (rc < 0 || rc >= linebuflen)
            goto error;
        linebufptr += rc;
        linebuflen -= rc;
    }

    rc = snprintf(linebufptr, linebuflen, "\n");
    if (rc < 0 || rc >= linebuflen)
        goto error;

    //assert(strlen(linebuf) == sizeof(linebuf) - linebuflen + 1);

    rc = write(wfd, linebuf, sizeof(linebuf) - linebuflen + 1);
    return (rc < 0) ? 1 : 0;

error:
    fprintf(stderr, "FATAL: line buffer overflow (%s)\n", strerror(errno));
    exit(EXIT_FAILURE);
}

static int lauditd_enqueue(int wfd, const char *device, int batch_size, long long *recpos)
{
    int                      flags  = CHANGELOG_FLAG_JOBID|
                                      CHANGELOG_FLAG_EXTRA_FLAGS;
    int                      batch_count = 0;
    void                    *ctx;
    struct changelog_rec    *rec;
    int                      rc;
    int                      status = LAUDITD_ENQUEUE_READER_FAILURE;
    ENTRY                    e;

    rc = llapi_changelog_start(&ctx, flags, device, *recpos + 1);
    if (rc < 0) {
        fprintf(stderr, "llapi_changelog_start device=%s recid=%lld rc=%d (%s)\n",
                device, *recpos + 1, rc, strerror(errno));
        goto exit_enqueue;
    }

    rc = llapi_changelog_set_xflags(ctx, CHANGELOG_EXTRA_FLAG_UIDGID |
                                         CHANGELOG_EXTRA_FLAG_NID |
                                         CHANGELOG_EXTRA_FLAG_OMODE);

    if (rc < 0) {
        fprintf(stderr, "llapi_changelog_set_xflags rc=%d\n", rc);
        goto exit_enqueue;
    }

    while ((rc = llapi_changelog_recv(ctx, &rec)) == 0) {

        e.key = (char *)changelog_type2str(rec->cr_type);

        if (!hsearch(e, FIND) && lauditd_writerec(wfd, device, rec)) {
            fprintf(stderr, "fifo writer failure (%s)\n", strerror(errno));
            status = LAUDITD_ENQUEUE_WRITER_FAILURE;
            break;
        }

        *recpos = rec->cr_index;

        rc = llapi_changelog_free(&rec);
        if (rc < 0) {
            fprintf(stderr, "llapi_changelog_free rc=%d\n", rc);
            break;
        }

        if (++batch_count >= batch_size)
            break;
    }

    if (rc == 0 && status != LAUDITD_ENQUEUE_WRITER_FAILURE) {
        status = LAUDITD_ENQUEUE_SUCCESS;
    }

    rc = llapi_changelog_fini(&ctx);
    if (rc) {
        fprintf(stderr, "llapi_changelog_fini rc=%d\n", rc);
        status = LAUDITD_ENQUEUE_READER_FAILURE;
    }

exit_enqueue:
    return status;
}

int main(int ac, char **av)
{
    struct                   sigaction ignore_action;
    struct                   sigaction term_action;
    const char              *mdtname = NULL;
    int                      c;
    int                      rc;
    int                      wfd;
    int                      batch_size = 1;
    char                     clid[64] = {0};
    char                     fifopath[PATH_MAX] = {0};
    char                    *fifodir;
    char                    *p;
    struct stat              statbuf;
    long long                recpos = 0LL;

    if (ac < 2) {
        usage();
        return 1;
    }

    if (!hcreate(IGNORE_TYPES_H_MAX)) {
        fprintf(stderr, "hcreate() failed: %s\n", strerror(errno));
        return 1;
    }

    // -u cl1 -f pipefile -b 1000 -i CLOSE,MASK
    while ((c = getopt(ac, av, "b:u:f:i:")) != -1) {
        switch (c) {
            case 'b':
                batch_size = atoi(optarg);
                break;
            case 'u':
                strncpy(clid, optarg, sizeof(clid));
                clid[sizeof(clid) - 1] = '\0';
                break;
            case 'f':
                strncpy(fifopath, optarg, sizeof(fifopath));
                fifopath[sizeof(fifopath) - 1] = '\0';
                fifodir = dirname(strdup(fifopath));
                break;
            case 'i':
                optarg = strdup(optarg);
                for (p = strtok(optarg,","); p != NULL; p = strtok (NULL, ",")) {
                    ENTRY e = {
                        .key = strdup(p),
                        .data = NULL
                    };
                    if (hsearch(e, ENTER) == NULL) {
			fprintf(stderr, "hsearch() failed: %s\n", strerror(errno));
			return 1;
                    }
                    fprintf(stderr, "ignoring record type %s\n", p);
                }
                break;
            case '?':
                usage();
                return 1;
        }
    }

    if (strlen(clid) == 0) {
        fprintf(stderr, "Missing changelog registration id (-u)\n");
        return 1;
    }
    if (strlen(fifopath) == 0) {
        fprintf(stderr, "Missing FIFO file path (-f)\n");
        return 1;
    }

    if (strlen(fifodir) == 0) {
        fprintf(stderr, "Invalid FIFO directory path\n");
        return 1;
    }

    ac -= optind;
    av += optind;

    if (ac < 1) {
        usage();
        return 2;
    }

    mdtname = av[0];

    /* Ignore SIGPIPEs -- can occur if the reader goes away. */
    memset(&ignore_action, 0, sizeof(ignore_action));
    ignore_action.sa_handler = SIG_IGN;
    sigemptyset(&ignore_action.sa_mask);
    rc = sigaction(SIGPIPE, &ignore_action, NULL);
    if (rc) {
        rc = -errno;
        fprintf(stderr, "cannot setup signal handler (%d): %s\n",
                SIGPIPE, strerror(-rc));
        return 1;
    }

    /* SIGTERM/SIGINT: clean named pipe on normal exit */
    memset(&term_action, 0, sizeof(term_action));
    term_action.sa_handler = lauditd_sigterm;
    sigemptyset(&term_action.sa_mask);
    rc = sigaction(SIGTERM, &term_action, NULL);
    if (rc) {
        rc = -errno;
        fprintf(stderr, "cannot setup signal handler (%d): %s\n",
                SIGTERM, strerror(-rc));
        return 1;
    }
    rc = sigaction(SIGINT, &term_action, NULL);
    if (rc) {
        rc = -errno;
        fprintf(stderr, "cannot setup signal handler (%d): %s\n",
                SIGINT, strerror(-rc));
        return 1;
    }

    /* Create directory if needed */
    if ((mkdir(fifodir, 0755) < 0) && (errno != EEXIST)) {
        rc = -errno;
        fprintf(stderr, "mkdir '%s' failed with error %d\n", fifodir, rc);
        return 1;
    }
    free(fifodir);

    /* Initialize FIFO */
    if ((mkfifo(fifopath, 0644) < 0) && (errno != EEXIST)) {
        rc = -errno;
        fprintf(stderr, "mkfifo failed with error %d\n", rc);
        return 1;
    }
    if (errno == EEXIST) {
        if (stat(fifopath, &statbuf) < 0) {
            fprintf(stderr, "stat(%s) failed\n", fifopath);
            return 1;
        }
        if (!S_ISFIFO(statbuf.st_mode) ||
            ((statbuf.st_mode & 0777) != 0644)) {
                fprintf(stderr, "%s exists but is not a pipe or has a wrong mode",
                        fifopath); 
            return 1;
        }
    }

    fprintf(stderr, "ready to write changelogs from MDT %s to FIFO %s\n",
            mdtname, fifopath);

    wfd = lauditd_openfifo(fifopath);

    while (1) {
        long long startrec = recpos;

        if (TerminateSig) {
            lauditd_cleanup(fifopath);
            break;
        }

        switch (lauditd_enqueue(wfd, mdtname, batch_size, &recpos)) {

            case LAUDITD_ENQUEUE_WRITER_FAILURE:
                close(wfd);
                if (TerminateSig) {
                    lauditd_cleanup(fifopath);
                    return 0;
                }
                wfd = lauditd_openfifo(fifopath);
                continue;

            case LAUDITD_ENQUEUE_READER_FAILURE:
                /* EOF or error from changelogs */
                sleep(1);

            case LAUDITD_ENQUEUE_SUCCESS:
                break;
        }

        if (recpos > startrec) {
            // clear changelogs read
            rc = llapi_changelog_clear(mdtname, clid, recpos);
            if (rc < 0) {
                fprintf(stderr, "llapi_changelog_clear recpos=%lld rc=%d\n",
                        recpos, rc);
            }
        }
    }

    return 0;
}
