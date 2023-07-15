#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/time.h>

#include <sqlite3.h>

#include "jdic.h"
#include "jmdict.h"

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
    while ((c = (char)getopt(argc, argv, ":hvfkrd:i:m:p:l:")) != -1) {
        switch (c) {
            case 'v':
                p.verbose++;
                break;
            case 'f':
                p.fast++;
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

    if (p.verbose >= 1) printf("Searching for \"%s\"...\n", arg);

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
        if (p.verbose >= 1) printf("Found %i match(es)\n", count);
    } else {
        printf("No results found...\n");
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
    char *kanji;
    int ntags;
    char **tags;
    char *reading;
    bool true_reading;
} kanji_t;

static long long mstime()
{
    struct timeval time;
    gettimeofday(&time, NULL);
    long long s1 = (long long)(time.tv_sec) * 1000;
    long long s2 = (time.tv_usec / 1000);
    return s1 + s2;
}

// TODO swap count queries for dynamic arrays
void print_kanji_info(jdic_t *p, int seqnum)
{
    struct sqlite3_stmt *st = NULL;
    int nkanji = 0;
    kanji_t *kanji = NULL;

    if (p->verbose >= 2) {
        printf("[%i] ", seqnum);
    }

    long long then = mstime();

    // TODO replace with dynamic array
    {
        const char *sql =
            "SELECT count(*) "
            "FROM ("
                "SELECT * FROM jmdict_kanji WHERE seqnum = ?"
            ") k "
                "LEFT JOIN jmdict_reading_for f ON f.kanji = k.id "
                "LEFT JOIN jmdict_reading r ON r.id = f.reading OR ("
                    "r.seqnum = k.seqnum AND NOT EXISTS("
                        "SELECT * FROM jmdict_reading_for WHERE kanji = k.id"
                    ")"
                ")";
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

    if (nkanji == 0) {
        {
            const char *sql = "SELECT count(*) FROM jmdict_reading WHERE seqnum = ?";
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

        {
            const char *sql = "SELECT text FROM jmdict_reading WHERE seqnum = ?";
            sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, seqnum);

            int ec = SQLITE_FAIL;
            for (int i = 0; (ec = sqlite3_step(st)) == SQLITE_ROW; i++) {
                kanji_t *k = &kanji[i];

                k->reading = strdup((const char *)sqlite3_column_text(st, 0));
                k->true_reading = false;

                if (i == 0) {
                    printf("%s\n", k->reading);
                }
            }
            if (ec != SQLITE_DONE) {
                fprintf(stderr, "ERR! Failed to parse all readings: %i\n", ec);

                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
    } else {
        kanji = calloc((size_t)nkanji, sizeof(kanji_t));

        const char *sql =
            "SELECT k.text, r.text, r.truereading "
            "FROM ("
                "SELECT * FROM jmdict_kanji WHERE seqnum = ?"
            ") k "
                "LEFT JOIN jmdict_reading_for f ON f.kanji = k.id "
                "LEFT JOIN jmdict_reading r ON r.id = f.reading OR ("
                    "r.seqnum = k.seqnum AND NOT EXISTS("
                        "SELECT * FROM jmdict_reading_for WHERE kanji = k.id"
                    ")"
                ")";

        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, seqnum);

        int ec = SQLITE_FAIL;
        for (int i = 0; (ec = sqlite3_step(st)) == SQLITE_ROW; i++) {
            kanji_t *k = &kanji[i];

            k->kanji = strdup((const char *)sqlite3_column_text(st, 0));
            k->reading = strdup((const char *)sqlite3_column_text(st, 1));
            k->true_reading = sqlite3_column_int(st, 2);

            if (i == 0) {
                if (k->true_reading) {
                    printf("%s【%s】\n", k->kanji, k->reading);
                } else {
                    printf("%s\n", k->reading);
                }
            }
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to parse all kanji: %i\n", ec);

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    if (p->verbose >= 2) {
        printf("     kanji query time = %llims\n", mstime() - then);
        then = mstime();
    }

    {
        {
            const char *sql =
                "SELECT sense, type, text FROM jmdict_sense_gloss "
                "WHERE seqnum = ? AND lang = ?";
            sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, seqnum);
            sqlite3_bind_text(st, 2, p->lang, 3, SQLITE_TRANSIENT);
        }

        struct sqlite3_stmt *st2 = NULL;
        struct sqlite3_stmt *st3 = NULL;
        int xrefid = 0;
        const char *xrefstr = NULL;
        if (p->fast < 1) {
            {
                const char *sql =
                    "SELECT sense, group_concat(text, ', ') "
                    "FROM jmdict_sense_pos WHERE seqnum = ? "
                    "GROUP BY sense";
                sqlite3_prepare_v2(p->db, sql, -1, &st2, NULL);
                sqlite3_bind_int(st2, 1, seqnum);
            }

            {
                const char *sql =
                    "SELECT sense, group_concat(text, ', ') "
                    "FROM jmdict_sense_xref WHERE seqnum = ? "
                    "GROUP BY sense";
                sqlite3_prepare_v2(p->db, sql, -1, &st3, NULL);
                sqlite3_bind_int(st3, 1, seqnum);

                int ec = sqlite3_step(st3);
                if (ec != SQLITE_ROW && ec != SQLITE_DONE) {
                    fprintf(stderr, "ERR! Failed to get xref: %i\n", ec);

                    goto d_cleanup;
                }

                xrefid = sqlite3_column_int(st3, 0);
                xrefstr = (const char *)sqlite3_column_text(st3, 1);
            }
        }

        if (p->verbose >= 2) {
            printf("      def1 query time = %llims\n", mstime() - then);
        }

        int lastid = 0;
        int ec = SQLITE_FAIL;
        for (int i = 0; (ec = sqlite3_step(st)) == SQLITE_ROW; i++) {
            definition_t def = {
                .id = sqlite3_column_int(st, 0),
                .text = (char *)sqlite3_column_text(st, 2),
            };
            const char *type = (const char *)sqlite3_column_text(st, 1);
            if (type != NULL) strcpy(def.type, type);

            if (def.id != lastid) {
                if (i > 0) {
                    if (p->fast < 1 && xrefid == def.id) {
                        if (xrefstr != NULL) printf("\t  See also %s\n", xrefstr);
                        xrefid = 0;
                        xrefstr = NULL;

                        int ec2 = sqlite3_step(st3);
                        if (ec2 == SQLITE_ROW) {
                            xrefid = sqlite3_column_int(st3, 0);
                            xrefstr = (const char *)sqlite3_column_text(st3, 1);
                        } else if (ec2 != SQLITE_DONE) {
                            fprintf(stderr, "ERR! Failed to get xref: %i\n", ec2);
                        }
                    }

                    putchar('\n');
                }

                if (p->fast < 1) {
                    int ec2 = sqlite3_step(st2);
                    if (ec2 == SQLITE_ROW) {
                        printf("\t%s\n", sqlite3_column_text(st2, 1));
                    } else if (ec2 != SQLITE_DONE) {
                        fprintf(stderr, "ERR! Failed to get part of speech: %i\n", ec2);
                    }
                }

                printf("\t%2i) %s\n", def.id, def.text);
            } else {
                printf("\t    %s\n", def.text);
            }
            lastid = def.id;
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to parse all definitions: %i\n", ec);
            goto cleanup;
        }
        if (p->fast < 1 && xrefstr != NULL && xrefid == lastid) {
            printf("\t  See also %s\n", xrefstr);
        }

    d_cleanup:
        sqlite3_finalize(st);
        if (st2 != NULL) sqlite3_finalize(st2);
        if (st3 != NULL) sqlite3_finalize(st3);
        st = NULL;
    }

    if (p->verbose >= 2) {
        printf("definition query time = %llims\n", mstime() - then);
    }

    if (nkanji > 1) {
        printf("\n    Other forms:\n\t    ");
        for (int i = 1; i < nkanji; i++) {
            kanji_t *k = &kanji[i];

            if (k->true_reading) {
                printf("%s【%s】", k->kanji, k->reading);
            } else {
                printf("%s", k->reading);
            }

            if (i != nkanji-1) {
                printf("、");
            }
        }
        putchar('\n');
    }
    putchar('\n');

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

        }
        free(kanji);
    };
}


void usage(const char *fn)
{
    printf(
            "usage: %s <query>\n"
            "\t-h\t\tDisplay this message\n"
            "\t-v\t\tEnable verbose output\n"
            "\t-f\t\tOmit extra info for faster output\n"
            "\t-k\t\tSearch kanji\n"
            "\t-r\t\tSearch reading (kana)\n"
            "\t-d <db.sqlite>\tUse specified database\n"
            "\t-i <file>\tImport dictionary file\n"
            "\t-m <max>\tMaximum number of entries to display, defaults to 4\n"
            "\t-p <page>\tPage number to display\n",
            fn
    );
}

