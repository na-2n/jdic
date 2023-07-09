#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>

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
    };
    search_mode_t search_mode = SEARCH_AUTO;

    if (argc == 1) {
        usage(argv[0]);
        return ret;
    }

    char c;
    while ((c = (char)getopt(argc, argv, ":hvkrd:i:m:p:")) != -1) {
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
            //print_kanji_info(seqnums[i]);
            printf("%i seqnum %i\n", i+1, seqnums[i]);
        }
    }

cleanup:
    free(seqnums);
    free(arg);
    return ret;
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

