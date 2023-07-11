#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

#include <time.h>

#include <sqlite3.h>

#include "jdic.h"
#include "jmdict.h"

#define DYNARR_GROW 2
#define DYNARR_INIT 4

typedef enum {
    SEARCH_AUTO = 0,
    SEARCH_KANJI,
    SEARCH_READING,
    SEARCH_BOTH,
} search_mode_t;

static void usage(const char *);
static void print_kanji_info(jdic_t *, int);

int main(int argc, char **argv)
{
    int ret = EXIT_SUCCESS;
    int iflag = 0; 
    char *ival = NULL;
    int dflag = 0;
    char *dval = NULL;
    jdic_t p = {
        .limit = 5,
        .page = 1,
        .lang = "eng",
    };
    search_mode_t search_mode = SEARCH_AUTO;

    if (argc == 1) {
        usage(argv[0]);
        return ret;
    }

    char c;
    while ((c = (char)getopt(argc, argv, ":hvkrd:i:m:p:l:")) != -1) {
        switch (c) {
            case 'v':
                p.verbose++;
                break;
            case 'k':
                search_mode = SEARCH_KANJI;
                break;
            case 'r':
                search_mode = SEARCH_READING;
                break;
            case 'd':
                dflag = 1;
                dval = optarg;
                break;
            case 'i':
                iflag = 1;
                ival = optarg;
                break;
            case 'm':
                p.limit = atoi(optarg);
                break;
            case 'p':
                p.page = atoi(optarg);
                break;
            case 'l':
                strcpy(p.lang, optarg);
                break;
            case ':':
                fprintf(stderr, "Missing required argument for -%c\n", optopt);

                usage(*argv);
                return EXIT_FAILURE;
            case '?':
                if (isprint(optopt)) {
                    fprintf(stderr, "Unknown option: -%c\n", optopt);

                    usage(*argv);
                    return EXIT_FAILURE;
                }
                break;
            case 'h':
            default:
                usage(argv[0]);
                return ret;
        }
    }

    int ec = sqlite3_open(dflag && dval != NULL ? dval : "db.sqlite3", &p.db);
    if (ec != SQLITE_OK) {
        fprintf(stderr, "Failed to open SQLite3 database\n");
        return EXIT_FAILURE;
    }

    if (iflag) {
        ret = jmdict_import(&p, ival);
        if (ret) {
            return ret;
        }
    }

    if (argc - optind <= 0) {
        fprintf(stderr, "No search query provided, aborting!\n");

        return EXIT_FAILURE;
    }

    char **args = argv+optind;
    char *arg = NULL;
    // this is pretty ugly...
    {
        size_t size = strlen(*args);
        for (int i = 1; i < argc - optind; i++) {
            size += 1 + strlen(args[i]);
        }
        size++;

        arg = calloc(size, sizeof(char));
        memcpy(arg, *args, strlen(*args));
        for (int i = 1; i < argc - optind; i++) {
            char *a = args[i];
            memcpy(arg+strlen(arg)+1, a, strlen(a));
            arg[strlen(arg)] = (argc - optind - 1) == i ? '\0' : ' ';
        }
    }

    printf("Searching for \"%s\"...\n", arg);

    int *seqnums = calloc((size_t)p.limit, sizeof(int));
    int count = -1;
    if (search_mode == SEARCH_AUTO) {
        count = jmdict_search_kanji(&p, arg, seqnums);
        if (count <= 0) {
            if (p.verbose) {
                fprintf(stderr, "No kanji results, trying reading...\n");
            }
            count = jmdict_search_reading(&p, arg, seqnums);
        }

        if (count <= 0) {
            if (p.verbose) {
                fprintf(stderr, "No reading results found either! aborting...\n");
            }
            fprintf(stderr, "No results found...\n");

            goto cleanup;
        }
    } else if (search_mode == SEARCH_KANJI) {
        count = jmdict_search_kanji(&p, arg, seqnums);
        if (count <= 0) {
            fprintf(stderr, "No kanji results found...\n");

            goto cleanup;
        }
    } else if (search_mode == SEARCH_READING) {
        count = jmdict_search_reading(&p, arg, seqnums);
        if (count <= 0) {
            fprintf(stderr, "No reading results found...\n");

            goto cleanup;
        }
    }

    if (count > 0) {
        for (int i = 0; i < count; i++) {
            print_kanji_info(&p, seqnums[i]);
        }
    }

cleanup:
    sqlite3_close(p.db);

    free(seqnums);
    free(arg);
    return ret;
}

typedef struct {
    int id;
    char lang[4];
    char type[5];
    char *text;
} definition_t;

typedef struct {
    int id;
    char *reading;
    bool true_reading;
} reading_t;

typedef struct {
    int id;
    char *kanji;
    int ntags;
    char **tags;
    int nreadings;
    int readings_a;
    reading_t *readings;
} kanji_t;

int ra_check(kanji_t *k, int size)
{
    if (k->readings_a < size) {
        reading_t *new = realloc(k->readings, (size_t)(k->readings_a * DYNARR_GROW));
        if (new == NULL) {
            return 1;
        }

        k->readings = new;
        k->readings_a *= DYNARR_GROW;
    }

    return 0;
}

// TODO swap count queries for dynamic arrays
void print_kanji_info(jdic_t *p, int seqnum)
{
    struct sqlite3_stmt *st = NULL;
    int nkanji = 0;
    kanji_t *kanji = NULL;
    int ndefs = 0;
    definition_t *defs = NULL;

    if (p->verbose >= 2) {
        printf("[%i] ", seqnum);
    }

    time_t then = time(0);

    {
        const char *sql = "SELECT count(*) FROM jmdict_kanji WHERE seqnum = ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);

        int ec = sqlite3_step(st);
        if (ec == SQLITE_ROW) {
            nkanji = sqlite3_column_int(st, 0);
        } else if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to get kanji count\n");

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    kanji = calloc((size_t)nkanji, sizeof(kanji_t));

    // TODO combine with reading for query
    if (nkanji > 0) {
        const char *sql = "SELECT id, text FROM jmdict_kanji WHERE seqnum = ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);

        int ec = SQLITE_FAIL;
        for (int i = 0; (ec = sqlite3_step(st)) == SQLITE_ROW; i++) {
            struct sqlite3_stmt *st2 = NULL;
            kanji_t *k = &kanji[i];

            k->id = sqlite3_column_int(st, 0);
            k->kanji = strdup((const char *)sqlite3_column_text(st, 1));
            k->readings = calloc(DYNARR_INIT, sizeof(reading_t));
            k->readings_a = DYNARR_INIT;

            {
                const char *sql = "SELECT count(*) FROM jmdict_kanji_tag WHERE kanji = ?";
                sqlite3_prepare_v2(p->db, sql, -1, &st2, NULL);
                sqlite3_bind_int(st2, 1, k->id);

                if (sqlite3_step(st2) == SQLITE_ROW) {
                    k->ntags = sqlite3_column_int(st2, 0);
                    k->tags = calloc((size_t)k->ntags, sizeof(char *));
                } else {
                    fprintf(stderr, "ERR! Failed to read kanji tag count\n");

                    goto k_next;
                }

                sqlite3_finalize(st2);
                st2 = NULL;
            }

            if (k->ntags > 0) {
                const char *sql = "SELECT text FROM jmdict_kanji_tag WHERE kanji = ?";
                sqlite3_prepare_v2(p->db, sql, -1, &st2, NULL);
                sqlite3_bind_int(st2, 1, k->id);

                int ec2 = SQLITE_FAIL;
                for (int ii = 0; (ec = sqlite3_step(st2)) == SQLITE_ROW; ii++) {
                    k->tags[ii] = strdup((const char *)sqlite3_column_text(st2, 0));
                }
                if (ec2 != SQLITE_DONE) {
                    fprintf(stderr, "ERR! Failed to get all kanji tags: %i\n", ec2);
                }
            }

        k_next:
            if (st2 != NULL) sqlite3_finalize(st2);
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to parse all kanji: %i\n", ec);

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    if (p->verbose >= 2) {
        printf("ktime = %lu\n", time(0) - then);
        then = time(0);
    }

    {
        const char *sql = "SELECT id, text, truereading FROM jmdict_reading WHERE seqnum = ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);

        int ec = SQLITE_FAIL;
        while ((ec = sqlite3_step(st)) == SQLITE_ROW) {
            struct sqlite3_stmt *st2 = NULL;
            reading_t r = {
                .id = sqlite3_column_int(st, 0),
                .true_reading = sqlite3_column_int(st, 2),
            };
            const char *text = (const char *)sqlite3_column_text(st, 1);
            int nfor = 0;

            {
                const char *sql = "SELECT count(*) FROM jmdict_reading_for WHERE reading = ?";
                sqlite3_prepare_v2(p->db, sql, -1, &st2, NULL);
                sqlite3_bind_int(st2, 1, r.id);

                int ec = sqlite3_step(st2);
                if (ec == SQLITE_ROW) {
                    nfor = sqlite3_column_int(st2, 0);
                } else {
                    fprintf(stderr, "ERR! Failed to get reading-for count\n");

                    goto r_next;
                }

                sqlite3_finalize(st2);
                st2 = NULL;
            }

            if (nfor > 0) {
                const char *sql = "SELECT kanji FROM jmdict_reading_for WHERE reading = ?";
                sqlite3_prepare_v2(p->db, sql, -1, &st2, NULL);
                sqlite3_bind_int(st2, 1, r.id);

                int ec = SQLITE_FAIL;
                while ((ec = sqlite3_step(st2)) == SQLITE_ROW) {
                    int kanji_id = sqlite3_column_int(st2, 0);

                    kanji_t *k = NULL;
                    for (int ii = 0; ii < nkanji; ii++) {
                        if (kanji[ii].id == kanji_id) {
                            k = &kanji[ii];
                            break;
                        }
                    }
                    if (k == NULL) {
                        fprintf(stderr, "ERR! Failed to find kanji this reading is for\n");

                        goto r_next;
                    }

                    if (ra_check(k, k->nreadings+1)) {
                        fprintf(stderr, "ERR! Failed to grow readings array\n");

                        goto r_next;
                    }
                    memcpy(&k->readings[k->nreadings], &r, sizeof(reading_t));
                    k->readings[k->nreadings].reading = strdup(text);
                    k->nreadings++;
                }
            } else {
                for (int ii = 0; ii < nkanji; ii++) {
                    kanji_t *k = &kanji[ii];

                    if (ra_check(k, k->nreadings+1)) {
                        fprintf(stderr, "ERR! Failed to grow readings array\n");

                        goto r_next;
                    }

                    memcpy(&k->readings[k->nreadings], &r, sizeof(reading_t));
                    k->readings[k->nreadings].reading = strdup(text);
                    k->nreadings++;
                }
            }
        r_next:
            if (st2 != NULL) sqlite3_finalize(st2);
        }

        sqlite3_finalize(st);
        st = NULL;
    }


    if (p->verbose >= 2) {
        printf("rtime = %lu\n", time(0) - then);
        then = time(0);
    }

    {
        const char *sql =
            "SELECT count(*) FROM jmdict_sense "
            "LEFT JOIN jmdict_sense_gloss "
            "ON jmdict_sense_gloss.sense = jmdict_sense.id "
            "WHERE jmdict_sense.seqnum = ? AND jmdict_sense_gloss.lang = ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);
        sqlite3_bind_text(st, 2, p->lang, 3, SQLITE_TRANSIENT);

        int ec = sqlite3_step(st);
        if (ec == SQLITE_ROW) {
            ndefs = sqlite3_column_int(st, 0);
        } else {
            fprintf(stderr, "ERR! Failed to get sense count: %i\n", ec);
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    defs = calloc((size_t)ndefs, sizeof(definition_t));

    {
        const char *sql =
            "SELECT jmdict_sense.id, jmdict_sense_gloss.type, jmdict_sense_gloss.text "
            "FROM jmdict_sense "
            "LEFT JOIN jmdict_sense_gloss ON jmdict_sense_gloss.sense = jmdict_sense.id "
            "WHERE jmdict_sense.seqnum = ? AND jmdict_sense_gloss.lang = ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);
        sqlite3_bind_text(st, 2, p->lang, 3, SQLITE_TRANSIENT);

        int ec = SQLITE_FAIL;
        for (int i = 0; (ec = sqlite3_step(st)) == SQLITE_ROW; i++) {
            definition_t *def = &defs[i];

            def->id = sqlite3_column_int(st, 0);
            const char *type = (const char *)sqlite3_column_text(st, 1);
            if (type != NULL) strcpy(def->type, type);
            def->text = strdup((const char *)sqlite3_column_text(st, 2));
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to parse all definitions: %i\n", ec);

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    if (p->verbose >= 2) {
        printf("dtime = %lu\n", time(0) - then);
    }

    {
        {
            kanji_t *k = &kanji[0];
            printf("%s", k->kanji);
            if (k->nreadings == 1) {
                printf("【%s】", k->readings[0].reading);
            } else {
                printf("【");
                for (int ii = 0; ii < k->nreadings; ii++) {
                    printf("%s", k->readings[ii].reading);
                    if (ii != k->nreadings-1) {
                        printf("、");
                    }
                }
                printf("】");
            }
            printf("\n");
        }

        if (ndefs > 0) {
            int lastid = 0;
            for (int i = 0; i < ndefs; i++) {
                definition_t *def = &defs[i];

                if (def->id != lastid) {
                    if (i > 0) putchar('\n');
                    printf("\t%2i) %s\n", i+1, def->text);
                } else {
                    printf("\t    %s\n", def->text);
                }

                lastid = def->id;
            }
        }

        if (nkanji > 1) {
            printf("\nOther forms:\n\t");
            for (int i = 1; i < nkanji; i++) {
                kanji_t *k = &kanji[i];

                printf("%s", k->kanji);
                if (k->nreadings == 1) {
                    printf("【%s】", k->readings[0].reading);
                } else {
                    printf("【");
                    for (int ii = 0; ii < k->nreadings; ii++) {
                        printf("%s", k->readings[ii].reading);
                        if (ii != k->nreadings-1) {
                            printf("、");
                        }
                    }
                    printf("】");
                }

                if (i != nkanji-1) {
                    printf("、");
                }
            }
            putchar('\n');
        }
        putchar('\n');
    }

cleanup:
    if (st != NULL) sqlite3_finalize(st);
    if (kanji != NULL) {
        for (int i = 0; i < nkanji; i++) {
            kanji_t *k = &kanji[i];

            free(k->kanji);

            for (int i = 0; i < k->ntags; i++) {
                free(k->tags[i]);
            }
            free(k->tags);

            for (int i = 0; i < k->nreadings; i++) {
                reading_t *r = &k->readings[i];

                free(r->reading);
            }
        }
        free(kanji);
    };
    if (defs != NULL) {
        for (int i = 0; i < ndefs; i++) {
            definition_t *def = &defs[i];

            free(def->text);
        }
        free(defs);
    }
}


void usage(const char *fn)
{
    printf(
            "usage: %s <query>\n"
            "\t-h\t\tDisplay this message\n"
            "\t-v\t\tEnable verbose output\n"
            "\t-k\t\tSearch kanji\n"
            "\t-r\t\tSearch reading (kana)\n"
            "\t-d <db.sqlite>\tUse specified database\n"
            "\t-i <file>\tImport dictionary file\n"
            "\t-m <max>\tMaximum number of entries to display, defaults to 4\n"
            "\t-p <page>\tPage number to display\n",
            fn
    );
}

