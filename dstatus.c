#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <fcntl.h>
#include <errno.h>
#include <stdio.h>
#include <pthread.h>
#include <X11/Xlib.h>

#define CONT 0
#define QUIT -1
#define FIFO "/dev/shm/dsfifo"
#define BUF_SIZE 100
#define BAR_LENGTH 200

#define DEFAULT_ELEM_FUN elem_remove
#define DEFAULT_FREQ_SEC 3
#define DEFAULT_FREQ_USEC 0

#define MIN(a,b) (((a)<(b))?(a):(b))
#define LENGTH(X) (sizeof X / sizeof X[0])

#define SEPARATOR   " | "
#define TRUNC_LEFT  "-"
#define TRUNC_RIGHT "-"

#define SEPLEN (sizeof(SEPARATOR) - 1)
#define TLEN_L (sizeof(TRUNC_LEFT) - 1)
#define TLEN_R (sizeof(TRUNC_RIGHT) - 1)

typedef struct Elem Elem;
struct Elem {
    char *name;
    char *text;
    int length;
    int freq_sec;
    int freq_usec;
    struct timeval expire;
    Elem *next;
    void (*fun)(Elem *e);
};

void func_time(Elem *prev);
void handle_sigalrm(int sig);
void *handle_updates(void *status);
void set_status(char *status);
int update_timer(Elem *e);
void update_status(void);
void elem_remove(Elem *prev);
void elem_tick(void);
void elem_trigger(Elem *e);
void elem_purge(void);
void elem_clear(void);
void elem_move(Elem *e, Elem *pos);
void elem_edit(Elem *prev, Elem *pos, char *text);
Elem *elem_find(char *name);
char *next_word(char **buf);
int handle_input(char *buf);
int main(void);

void
func_time(Elem *prev) {
    elem_edit(prev, NULL, "TEST ABCD");
    return;
};

typedef struct {
    char *name;
    void (*fun)(Elem *e);
} Function;

const Function funs[] = {
    { "time", func_time }
};

Display *disp;
Elem *elements;
int totallen;

struct itimerval nextupdate;

void
handle_sigalrm(int sig) {
    char buf[16];
    int fd;
    sig = sig;
    printf("alarm called! aaaaa.\n");

    fd = open(FIFO, O_WRONLY);
    if(fd == -1) {
        perror("Child could not open");
        return;
    }
    strncpy(buf, "-u", 3);
    write(fd, buf, strlen(buf));
    close(fd);
    return;
}

void *
handle_updates(void *status) {
    int sig;
    sigset_t sigset;
    status = status;

    sigfillset(&sigset);
    sigdelset(&sigset, SIGALRM);
    signal(SIGALRM, handle_sigalrm);
    sigwait(&sigset, &sig);

    printf("thread finishes\n");
    return NULL;
}

void
set_status(char *status) {
    int len = strlen(status), i;
    printf("Setting status\n");

    for(i = 0; i < len; i++) {
        printf("%02x ", status[i]);
    }
    printf("\n");

    XStoreName(disp, DefaultRootWindow(disp), status);
    printf("Setting status cont.\n");
    XSync(disp, False);
    //XFlush(disp);
    printf("Done setting status.\n");
    return;
}

int
update_timer(Elem *e) {
    struct timeval cur, ttl;
    long carry = 0;
    gettimeofday(&cur, NULL);
    ttl.tv_usec = e->expire.tv_usec - cur.tv_usec;
    if(ttl.tv_usec < 0) {
        carry = 1;
        ttl.tv_usec += 1000000;
    }
    ttl.tv_sec = e->expire.tv_sec - cur.tv_sec - carry;

    if(ttl.tv_sec < 0 || (!ttl.tv_sec && ttl.tv_usec <= 0)) {
        ttl.tv_sec = e->freq_sec;
        ttl.tv_usec = e->freq_usec;
    }

    if((!nextupdate.it_value.tv_sec && !nextupdate.it_value.tv_usec)
            || ttl.tv_sec < nextupdate.it_value.tv_sec
            || (ttl.tv_sec == nextupdate.it_value.tv_sec
                && ttl.tv_usec <= nextupdate.it_value.tv_usec)) {
        printf("SETTING TIMER TO GO OFF IN: %ld.%02ld seconds.\n", ttl.tv_sec, ttl.tv_usec);
        nextupdate.it_value.tv_sec = ttl.tv_sec;
        nextupdate.it_value.tv_usec = ttl.tv_usec;
        return 1;
    }
    return 0;
}

void
update_status(void) {
    Elem *e = elements->next;
    int written = 0, skipped = 0, elen;
    int write_start, elem_end, free_space, resv_space;
    char buf[BAR_LENGTH + 1];
    buf[0] = 0;
    printf("Starting update_status()\n");

    if(!e) {
        set_status(" ");
        return;
    }

    while(e && totallen - skipped > BAR_LENGTH - written) {
        elen = e->length + (e->next ? SEPLEN : 0);
        write_start = BAR_LENGTH - written;
        elem_end = totallen - skipped - elen;
        free_space = write_start - elem_end;
        resv_space = TLEN_R + (e->next ? SEPLEN : 0);
        if(write_start >= elem_end && free_space > resv_space) {
            strncat(buf, e->text, free_space - resv_space);
            strcat(buf, TRUNC_RIGHT);
            written += free_space;
            if(e->next)
                strcat(buf, SEPARATOR);
        } else if(!skipped) {
            strcat(buf, TRUNC_LEFT);
            written += TLEN_L;
            if(e->next)
                strcat(buf, SEPARATOR);
        }
        skipped += elen;
        e = e->next;
    }

    for(; e; e = e->next) {
        strcat(buf, e->text);
        if(e->next)
            strcat(buf, SEPARATOR);
    }

    printf("Ending update_status()\n");
    set_status(buf);
    printf("update_status() done\n");
    return;
}

void
elem_remove(Elem *prev) {
    Elem *e = prev->next;
    printf("REMOVING. e->next: %p, length: %d + %lu\n", e->next, e->length, e->next ? SEPLEN : 0);
    totallen -= e->length + (totallen > e->length ? SEPLEN : 0);
    prev->next = e->next;
    printf("Removed element, totallen is now: %d\n", totallen);
    if(e->name)
        free(e->name);
    free(e->text);
    free(e);
    return;
}

void
elem_tick(void) {
    Elem *prev = elements, *e;
    struct timeval cur;
    gettimeofday(&cur, NULL);
    printf("elem_tick called @@@@@@@@@@@@@@@@@@@@@@@@@@@@@\n");

    nextupdate.it_value.tv_sec = 0;
    nextupdate.it_value.tv_usec = 0;
    getitimer(ITIMER_REAL, &nextupdate);
    while((e = prev->next)) {
        if(e->freq_sec || e->freq_usec) {

            if(cur.tv_sec > e->expire.tv_sec
                    || (cur.tv_sec == e->expire.tv_sec
                        && cur.tv_usec >= e->expire.tv_usec)) {
                e->expire.tv_sec = cur.tv_sec + e->freq_sec;
                e->expire.tv_usec = cur.tv_usec + e->freq_usec;

                if(e->fun)
                    e->fun(prev);
            }
            if(prev->next == e) {
                update_timer(e);
                prev = e;
            }
        }
    }
    printf("ACTUALLY SETTING TIMER NOW: %ld.%02ld seconds.\n", nextupdate.it_value.tv_sec, nextupdate.it_value.tv_usec);
    setitimer(ITIMER_REAL, &nextupdate, NULL);

    return;
}

void
elem_trigger(Elem *e) {
    e->expire.tv_sec = 0;
    e->expire.tv_usec = 0;

    elem_tick();
    return;
}

void
elem_purge(void) {
    Elem *e = elements;
    while(e->next)
        if(!e->next->name)
            elem_remove(e);
        else
            e = e->next;
    return;
}

void
elem_clear(void) {
    while(elements->next)
        elem_remove(elements);
    return;
}

Elem
*elem_prev(Elem *e) {
    Elem *tmp = elements;
    if(e == tmp)
        return NULL;
    while(tmp->next != e)
        tmp = tmp->next;
    return tmp;
}

void
elem_move(Elem *e, Elem *pos) {
    Elem *tmp = e->next;

    if(e->next == pos && !pos->next)
        return;

    if(e == pos) {
        if(!(pos = elem_prev(pos)))
            return;
    } else if(e->next == pos) {
        pos = tmp->next;
    }

    e->next = tmp->next;

    tmp->next = pos->next;
    pos->next = tmp;
    return;
};

void
elem_edit(Elem *prev, Elem *pos, char *text) {
    Elem *e = prev->next;
    char *s;
    int len;
    if(text) {
        len = strlen(text);
        if(len > e->length) {
            if((s = realloc(e->text, len + 1)))
                e->text = s;
            else
                fprintf(stderr, "dstatus: elem_edit failed to allocate memory for text '%s': %s\n", text, strerror(errno));
        }

        strncpy(e->text, text, len);
        e->text[len] = 0;
    }

    if(pos)
        elem_move(prev, pos);

    return;
}

void
elem_create(char *name, Elem *pos, char *text, int freq_sec, int freq_usec, void (*fun)(Elem *)) {
    Elem *e = malloc(sizeof(Elem));
    int len = strlen(text);
    struct timeval cur;
    gettimeofday(&cur, NULL);
    printf("create called. e is: %p, text is: %s\n", e, text);

    e->name = name;
    if(len) {
        e->text = malloc(len + 1);
        strcpy(e->text, text);
        e->length = len;
    }
    e->fun = fun;
    // error here? update length stuff if element is created with one length but then later edited
    totallen += len;
    if(elements->next)
        totallen += SEPLEN;

    printf("About to create element, it has time set: %d, %d\n", freq_sec, freq_usec);
    e->freq_sec = freq_sec;
    e->freq_usec = freq_usec;
    if(freq_sec || freq_usec) {
        printf("Creating element, it has time set: %d, %d\n", freq_sec, freq_usec);
        e->expire.tv_sec = cur.tv_sec + freq_sec;
        e->expire.tv_usec = cur.tv_usec + freq_usec;
        if(e->expire.tv_usec < cur.tv_usec)
            e->expire.tv_sec++;
        printf("Expire time is: %lu, %lu\n", e->expire.tv_sec, e->expire.tv_usec);
        printf("Cur    time is: %lu, %lu\n", cur.tv_sec, cur.tv_usec);


        getitimer(ITIMER_REAL, &nextupdate);
        if(update_timer(e)) {
            printf("Timer is updated in elem_create.\n");
            setitimer(ITIMER_REAL, &nextupdate, NULL);
        } else {
            printf("Timer is NOT UPDATED in elem_cerate.\n");
        }

    }

    printf("Got: %s. freq_sec = %d, freq_usec = %d\n", text, e->freq_sec, e->freq_usec);

    e->next = pos->next;
    pos->next = e;
    if(fun != DEFAULT_ELEM_FUN)
        fun(pos);
    printf("create elem finished\n");
}

Elem *
elem_find(char *name) {
    Elem *e;
    if(!name)
        return NULL;
    printf("trying to find: %s.\n", name);
    for(e = elements; e && e->next; e = e->next)
        if(e->next->name && !strcmp(e->next->name, name)) {
            printf("MATCH: found element named: %s\n", e->next->name);
            return e;
        }
    return NULL;
}

char *
next_word(char **buf) {
    char *end;
    if((end = strchr(*buf, ' ')))
        *end = 0;
    return end;
}

int
handle_input(char *buf) {
    Elem *e = (Elem *)-1, *pos = NULL, *tmp;
    char *name = NULL, *eow, c;
    int freq_sec = DEFAULT_FREQ_SEC, freq_usec = DEFAULT_FREQ_USEC;
    int d1 = 0, d2 = 0;
    unsigned int i;
    void (*fun)(Elem *e) = NULL;

    while(*buf == '-') {
        eow = next_word(&buf), c;
        printf("ITERATION. buf is: %s\n", buf);
        c = *++buf;
        buf += 2;
        switch(c) {
        case 'a':
        case 'b':
            if(eow) {
                eow = next_word(&buf);
                pos = elem_find(buf);
                if(c == 'a' && pos)
                    pos = pos->next;
            }
            break;
        case 'c':
            elem_clear();
            break;
        case 'd':
            if(eow) {
                eow = next_word(&buf);
                if(sscanf(buf, "%d.%1d%1d", &freq_sec, &d1, &d2) > 1)
                    freq_usec = (d1 * 10 + d2) * 10000;
                printf("sec: %d, usec: %d (decimal1: %d, decimal2: %d)\n", freq_sec, freq_usec, d1, d2);
            }
            break;
        case 'e':
            for(tmp = elements; tmp->next; tmp = tmp->next)
                ;
            pos = tmp;
            break;
        case 'f':
            if(eow) {
                eow = next_word(&buf);
                for(i = 0; i < LENGTH(funs); i++) {
                    if(!strcmp(funs[i].name, buf)) {
                        fun = funs[i].fun;
                        break;
                    }
                }
            }
            break;
        case 'k':
            if(eow) {
                eow = next_word(&buf);
                if((tmp = elem_find(buf)))
                    elem_remove(tmp);
            }
            break;
        case 'l':
            tmp = elements;
            while(tmp->next)
                tmp = tmp->next;
            elem_move(elements, tmp);
            break;
        case 'n':
            if(eow && !name) {
                eow = next_word(&buf);
                if((name = malloc(strlen(buf) + 1))) {
                    strcpy(name, buf);
                } else {
                    fprintf(stderr, "dstatus: Could not allocate element name '%s': %s\n", buf, strerror(errno));
                }
            }
            break;
        case 'p':
            elem_purge();
            break;
        case 'r':
            tmp = elements;
            while(tmp->next && tmp->next->next)
                tmp = tmp->next;
            elem_move(tmp, elements);
            break;
        case 't':
            if(eow) {
                eow = next_word(&buf);
                if((tmp = elem_find(buf)))
                    elem_trigger(tmp);
            }
            break;
        case 'u':
            elem_tick();
            break;
        case '-':
            if(eow)
                *eow = ' ';
            buf--;
            eow = (char *)-1;
            break;
        }

        if(eow == (char *)-1) {
            break;
        } else if(eow) {
            buf = eow + 1;
        } else {
            buf += strlen(buf);
            break;
        }
    }

    if(e == (Elem *)-1)
        e = elem_find(name);
    if(!e && (fun || *buf)) {
        printf("calling creat, freq_sec is: %d, freq_usec is: %d\n", freq_sec, freq_usec);
        elem_create(name, pos ? pos : elements, buf, freq_sec, freq_usec, fun ? fun : DEFAULT_ELEM_FUN);
    } else if(e && name) {
        elem_edit(e, pos, buf);
    }

    update_status();
    printf("done\n");

    // Always return at least 1
    return CONT;
}

int
main(void) {
    char buf[BUF_SIZE], *str;
    pthread_t updater;
    struct stat st;
    int fd, n, status = CONT;
    sigset_t sigset;

    elements = malloc(sizeof(Elem));
    elements->next = NULL;
    totallen = 0;

    nextupdate.it_interval.tv_sec = 0;
    nextupdate.it_interval.tv_usec = 0;
    nextupdate.it_value.tv_sec = 0;
    nextupdate.it_value.tv_usec = 0;

    if(!(disp = XOpenDisplay(NULL))) {
        fprintf(stderr, "dstatus: cannot open display.\n");
        return 1;
    }

    if(mkfifo(FIFO, S_IRUSR | S_IWUSR) == -1) {
        if(errno != EEXIST) {
            perror("Error: Could not create fifo file");
            return EXIT_FAILURE;
        } else {
            stat(FIFO, &st);
            if(!(st.st_mode & S_IFIFO) || access(FIFO, R_OK | W_OK)) {
                fprintf(stderr, "Error: Fifo file lacks permissions or is not a fifo.\n");
                return EXIT_FAILURE;
            }
        }
    }

    pthread_create(&updater, NULL, handle_updates, (void *)&status);

    status = status;

    sigemptyset(&sigset);
    sigaddset(&sigset, SIGALRM);
    sigprocmask(SIG_SETMASK, &sigset, NULL);

    while(status == CONT) {
        printf("\n\nstarting iteration reading file input.\n");
        fd = open(FIFO, O_RDONLY);
        if(fd == -1) {
            perror("Error: Could not read fifo");
            break;
        }
        if((n = read(fd, buf, BUF_SIZE - 1)))
            buf[n] = 0;
        if((str = strchr(buf, '\n')))
            *str = 0;
        close(fd);
        printf("STARTING, buf is: %s\n", buf);
        if(*buf && (status = handle_input(buf)) < CONT) {
            printf("Setting thread to quit.\n");
            status = QUIT;
            printf("Waiting for thread to quit.\n");
            if(!(n = pthread_join(updater, NULL)))
                fprintf(stderr, "dstatus: Failed to join updater thread. Error: %d\n", n);
            printf("Thread quitted. Removing elements.\n");
            elem_clear();
            free(elements);
        }
    }

    XCloseDisplay(disp);

    return 0;
}

