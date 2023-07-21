#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include <sqlite3.h>
#include <expat.h>
#include "util.h"
#include "jmdict.h"

// xml file read buffer size
#define XMLBUFSIZ 1 << 15
// commit sqlite db every X entries
#define COMMIT_FREQ 50000

typedef struct {
    int verbose;
    struct sqlite3 *db;
    XML_Parser parser;
    int depth;
    int count;

    int seqnum;
    int kanji_id;
    int reading_id;

    int sensei;

    const XML_Char *cur_tag;
    const XML_Char **cur_atts;
    XML_Char *cur_val;
    int cur_val_len;
    int cur_val_alen;
} userdata_t;

static void XMLCALL startEl(void *p, const XML_Char *name, const XML_Char **atts)
{
    userdata_t *d = (userdata_t *)p;

    d->depth++;

    if (d->depth == 1 && strcmp(name, "JMdict") != 0) {
        fprintf(stderr, "Invalid document: root node name does not match\n");

        XML_StopParser(d->parser, XML_FALSE);
        return;
    }

    if (d->depth == 2 && strcmp(name, "entry") != 0) {
        fprintf(stderr, "Invalid document: entry node name does not match\n");

        XML_StopParser(d->parser, XML_FALSE);
        return;
    }

    if (!strcmp(name, "sense")) {
        d->sensei++;
    }

    d->cur_tag = name;
    d->cur_atts = atts;
}

static void XMLCALL charHandler(void *p, const XML_Char *s, int len)
{
    if (*s == '\n') {
        return;
    }

    userdata_t *d = (userdata_t *)p;

    // accumulate all characters so we don't end up with partial strings
    // this is a big problem when using smaller buffer sizes, but could
    // cause problems with any buffer size
    if (d->cur_val_len + len > d->cur_val_alen) {
        void *ptr = realloc((void *)d->cur_val, (size_t)(d->cur_val_len + len) * sizeof(XML_Char));
        if (ptr == NULL) {
            fprintf(stderr, "Failed to (re)allocate memory for value string\n");

            XML_StopParser(d->parser, XML_FALSE);
            return;
        }
        d->cur_val = ptr;
        d->cur_val_alen = d->cur_val_len + len;

        if (d->verbose == 2) {
            printf("NEW cur_val BUF SIZE = %i\n", d->cur_val_alen);
        }
    }
    memcpy(d->cur_val+d->cur_val_len, s, (size_t)len);
    d->cur_val_len += len;
}

static void XMLCALL endEl(void *p, const XML_Char *name)
{
    userdata_t *d = (userdata_t *)p;
    struct sqlite3_stmt *st = NULL;

    if (!strcmp(name, "entry")) {
        if (d->verbose) {
            printf("Inserted entry #%i\n", d->seqnum);
        }

        // these are technically not needed since they *should* be overwritten before being used again
        d->kanji_id = 0;
        d->reading_id = 0;
        d->sensei = 0;
        d->count++;

        if (d->seqnum % COMMIT_FREQ == 0) {
            {
                sqlite3_prepare_v2(d->db, "COMMIT", -1, &st, NULL);
                int rc = sqlite3_step(st);
                if (rc != SQLITE_DONE) {
                    fprintf(stderr, "ERR! Failed to commit database transaction: %i\n", rc);

                    XML_StopParser(d->parser, XML_FALSE);
                    goto cleanup;
                }

                sqlite3_finalize(st);
                st = NULL;
            }
            {
                sqlite3_prepare_v2(d->db, "BEGIN", -1, &st, NULL);
                int rc = sqlite3_step(st);
                if (rc != SQLITE_DONE) {
                    fprintf(stderr, "ERR! Failed to begin new database transaction: %i\n", rc);

                    XML_StopParser(d->parser, XML_FALSE);
                }

                sqlite3_finalize(st);
                st = NULL;
            }
        }
    } else if (!strcmp(name, "ent_seq")) {
        d->seqnum = antoi(d->cur_val, (size_t)d->cur_val_len);
    } else if (!strcmp(name, "keb")) {
        {
            const char *sql = "INSERT INTO jmdict_kanji (seqnum, text) VALUES (?, ?)";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, d->seqnum);
            sqlite3_bind_text(st, 2, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

            int rc = sqlite3_step(st);
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "ERR! Failed to insert kanji: %i\n" , rc);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
        {
            const char *sql = "SELECT last_insert_rowid()";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);

            int rc = sqlite3_step(st);
            if (rc == SQLITE_ROW) {
                d->kanji_id = sqlite3_column_int(st, 0);
            } else {
                fprintf(stderr, "ERR! Failed to get last row id: %i\n", rc);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
    } else if (!strcmp(name, "reb")) {
        {
            const char *sql = "INSERT INTO jmdict_reading (seqnum, text) VALUES (?, ?)";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, d->seqnum);
            sqlite3_bind_text(st, 2, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

            int rc = sqlite3_step(st);
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "ERR! Failed to insert kanji: %i\n" , rc);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
        {
            const char *sql = "SELECT last_insert_rowid()";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);

            int rc = sqlite3_step(st);
            if (rc == SQLITE_ROW) {
                d->reading_id = sqlite3_column_int(st, 0);
            } else {
                fprintf(stderr, "ERR! Failed to get last row id: %i\n", rc);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
    // these are not of interest to us
    //} else if (!strcmp(name, "ke_pri")) {
    //} else if (!strcmp(name, "re_pri")) {
    } else if (!strcmp(name, "ke_inf")) {
        const char *sql = "INSERT INTO jmdict_kanji_tag (kanji, text) VALUES (?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->kanji_id);
        sqlite3_bind_text(st, 2, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert kanji tag: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "re_inf")) {
        const char *sql = "INSERT INTO jmdict_reading_tag (reading, text) VALUES (?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->reading_id);
        sqlite3_bind_text(st, 2, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert reading tag: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "gloss")) {
        const XML_Char *lang = "eng";
        const XML_Char *type = NULL;
        const XML_Char *gender = NULL;
        for (int i = 0; d->cur_atts[i]; i += 2) {
            const XML_Char *att = d->cur_atts[i];
            const XML_Char *attval = d->cur_atts[i+1];

            if (!strcmp(att, "xml:lang")) {
                lang = attval;
            } else if (!strcmp(att, "g_type")) {
                type = attval;
            } else if (!strcmp(att, "g_gend")) {
                gender = attval;
            }
        }

        const char *sql =
            "INSERT INTO jmdict_sense_gloss (seqnum, sense, lang, text, type, gender) "
            "VALUES (?, ?, ?, ?, ?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->seqnum);
        sqlite3_bind_int(st, 2, d->sensei);
        sqlite3_bind_text(st, 3, lang, (int)strlen(lang), SQLITE_TRANSIENT);
        sqlite3_bind_text(st, 4, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);
        if (type != NULL) {
            sqlite3_bind_text(st, 5, type, (int)strlen(type), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(st, 5);
        }
        if (gender != NULL) {
            sqlite3_bind_text(st, 6, gender, (int)strlen(gender), SQLITE_TRANSIENT);
        } else {
            sqlite3_bind_null(st, 6);
        }

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert glossary: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "pos")) {
        const char *sql =
            "INSERT INTO jmdict_sense_pos (seqnum, sense, text) "
            "VALUES (?, ?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->seqnum);
        sqlite3_bind_int(st, 2, d->sensei);
        sqlite3_bind_text(st, 3, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert pos: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "xref")) {
        const char *sql =
            "INSERT INTO jmdict_sense_xref (seqnum, sense, text) "
            "VALUES (?, ?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->seqnum);
        sqlite3_bind_int(st, 2, d->sensei);
        sqlite3_bind_text(st, 3, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert xref: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "s_inf")) {
        const char *sql =
            "INSERT INTO jmdict_sense_info (seqnum, sense, text) "
            "VALUES (?, ?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->seqnum);
        sqlite3_bind_int(st, 2, d->sensei);
        sqlite3_bind_text(st, 3, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert s_inf: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    } else if (!strcmp(name, "misc")) {
        const char *sql =
            "INSERT INTO jmdict_sense_misc (seqnum, sense, text) "
            "VALUES (?, ?, ?)";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->seqnum);
        sqlite3_bind_int(st, 2, d->sensei);
        sqlite3_bind_text(st, 3, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to insert misc: %i\n" , rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    //} else if (!strcmp(name, "lsource")) {
    //} else if (!strcmp(name, "ant")) {
    //} else if (!strcmp(name, "dial")) {
    //} else if (!strcmp(name, "stagk")) {
    //} else if (!strcmp(name, "stagk")) {
    } else if (!strcmp(name, "re_restr")) {
        int kanji_id;

        {
            const char *sql = "SELECT id FROM jmdict_kanji WHERE seqnum = ? AND text = ?";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, d->seqnum);
            sqlite3_bind_text(st, 2, d->cur_val, d->cur_val_len, SQLITE_TRANSIENT);

            int rc = sqlite3_step(st);
            if (rc == SQLITE_ROW) {
                kanji_id = sqlite3_column_int(st, 0);
            } else {
                fprintf(stderr, "ERR! Failed to get kanji id: %i (#%i)\n", rc, d->seqnum);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
        {
            const char *sql = "INSERT INTO jmdict_reading_for (reading, kanji) VALUES (?, ?)";
            sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
            sqlite3_bind_int(st, 1, d->reading_id);
            sqlite3_bind_int(st, 2, kanji_id);

            int rc = sqlite3_step(st);
            if (rc != SQLITE_DONE) {
                fprintf(stderr, "ERR! Failed to insert re_restr: %i\n", rc);

                XML_StopParser(d->parser, XML_FALSE);
                goto cleanup;
            }

            sqlite3_finalize(st);
            st = NULL;
        }
    } else if (!strcmp(name, "re_nokanji")) {
        const char *sql = "UPDATE jmdict_reading SET truereading = FALSE WHERE id = ?";
        sqlite3_prepare_v2(d->db, sql, -1, &st, NULL);
        sqlite3_bind_int(st, 1, d->reading_id);

        int rc = sqlite3_step(st);
        if (rc != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to set truereading for reading %i: %i\n", d->reading_id, rc);

            XML_StopParser(d->parser, XML_FALSE);
            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

cleanup:
    if (st != NULL) {
        sqlite3_finalize(st);
    }

    d->depth--;
    d->cur_val_len = 0;
}

int jmdict_import(jdic_t *p, const char *fn)
{
    time_t start = time(NULL);
    FILE *fp = fopen(fn, "r");
    if (!fp) {
        fprintf(stderr, "Failed to open file: %s\n", fn);

        return 1;
    }

    XML_Parser parser = XML_ParserCreate(NULL);
    if (!parser) {
        fprintf(stderr, "Could not allocate enough memory for XML parser!\n");

        fclose(fp);
        return 1;
    }

    userdata_t userdata = {
        .verbose = p->verbose,
        .parser = parser,
        .db = p->db,
    };
    int done = 0;
    int ret = 0;
    sqlite3_stmt *st = NULL;

    XML_SetParamEntityParsing(parser, XML_PARAM_ENTITY_PARSING_ALWAYS);
    XML_SetUserData(parser, &userdata);
    XML_SetElementHandler(parser, startEl, endEl);
    XML_SetCharacterDataHandler(parser, charHandler);

    /*
    sqlite3_prepare_v2(p->db, "PRAGMA foreign_keys = ON", -1, &st, NULL);
    if (sqlite3_step(st) != SQLITE_DONE) {
        fprintf(stderr, "Failed to enable foreign keys in SQLite database\n");

        ret = 1;
        goto cleanup;
    }
    sqlite3_finalize(st);
    */
    sqlite3_exec(p->db, "PRAGMA read_uncomitted=true", NULL, NULL, NULL);

    sqlite3_prepare_v2(p->db, "BEGIN", -1, &st, NULL);
    if (sqlite3_step(st) != SQLITE_DONE) {
        fprintf(stderr, "Failed to BEGIN SQLite transaction\n");

        ret = 1;
        goto cleanup;
    }
    sqlite3_finalize(st);

    do {
        void *buf = XML_GetBuffer(parser, XMLBUFSIZ);
        if (!buf) {
            fprintf(stderr, "Could not allocate enough memory for buffer\n");

            ret = 1;
            break;
        }

        size_t len = fread(buf, 1, XMLBUFSIZ, fp);
        if (ferror(fp)) {
            fprintf(stderr, "Read error\n");

            ret = 1;
            break;
        }

        done = feof(fp);
        if (XML_ParseBuffer(parser, (int)len, done) == XML_STATUS_ERROR) {
            fprintf(stderr,
                    "Parser error at line %lu:\n%s\n",
                    XML_GetCurrentLineNumber(parser),
                    XML_ErrorString(XML_GetErrorCode(parser)));

            ret = 1;
            break;
        }
    } while (!done);

    sqlite3_prepare_v2(p->db, "COMMIT", -1, &st, NULL);
    ret = sqlite3_step(st) != SQLITE_DONE;

    time_t taken = time(NULL) - start;
    time_t min = taken / 60;
    time_t sec = taken % 60;
    time_t hour = min / 60;
    min %= 60;

    char tstr[64];
    if (hour > 0) {
        sprintf(tstr, "%lih %lim %lis", hour, min, sec);
    } else if (min > 0) {
        sprintf(tstr, "%lim %lis", min, sec);
    } else {
        sprintf(tstr, "%lis", sec);
    }

    printf("Imported %i entries in %s\n", userdata.count, tstr);

    printf("Creating indices...\n");
    sqlite3_exec(p->db, "CREATE INDEX k_seqnum ON jmdict_kanji (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX k_text ON jmdict_kanji (text)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX r_seqnum ON jmdict_reading (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX r_id ON jmdict_reading (id)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX r_text ON jmdict_reaidng (text)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX f_kanji ON jmdict_reading_for (kanji)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX g_seqnum ON jmdict_sense_gloss (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX g_lang ON jmdict_sense_gloss (lang)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX p_seqnum ON jmdict_sense_pos (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX p_sense ON jmdict_sense_pos (sense)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX x_seqnum ON jmdict_sense_xref (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX x_sense ON jmdict_sense_xref (sense)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX i_seqnum ON jmdict_sense_info (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX i_sense ON jmdict_sense_info (sense)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX m_seqnum ON jmdict_sense_misc (seqnum)", NULL, NULL, NULL);
    sqlite3_exec(p->db, "CREATE INDEX m_sense ON jmdict_sense_misc (sense)", NULL, NULL, NULL);

cleanup:
    sqlite3_finalize(st);

    XML_ParserFree(parser);
    fclose(fp);
    free(userdata.cur_val);
    return ret;
}

int jmdict_search_kanji(jdic_t *p, const char *query, int *a)
{
    struct sqlite3_stmt *st = NULL;
    int count = 0;

    {
        const char *sql = "SELECT DISTINCT seqnum FROM jmdict_kanji WHERE text GLOB ? LIMIT ? OFFSET ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_text(st, 1, query, (int)strlen(query), SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, p->limit);
        sqlite3_bind_int(st, 3, (p->page - 1) * p->limit);

        int ec;
        while ((ec = sqlite3_step(st)) == SQLITE_ROW) {
            a[count] = sqlite3_column_int(st, 0);
            count++;
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to get count of matching entries\n");

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

    /*
    if (count == 0) {
        goto cleanup;
    }

    {
        const char *sql = "SELECT DISTINCT seqnum FROM jmdict_kanji WHERE text GLOB ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_text(st, 1, query, (int)strlen(query), SQLITE_TRANSIENT);

        sqlite3_finalize(st);
        st = NULL;
    }
    */

cleanup:
    if (st != NULL) {
        sqlite3_finalize(st);
    }

    return count;
}

int jmdict_search_reading(jdic_t *p, const char *query, int *a)
{
    struct sqlite3_stmt *st = NULL;
    int count = 0;

    {
        const char *sql = "SELECT DISTINCT seqnum FROM jmdict_reading WHERE text GLOB ? LIMIT ? OFFSET ?";
        sqlite3_prepare_v2(p->db, sql, -1, &st, NULL);
        sqlite3_bind_text(st, 1, query, (int)strlen(query), SQLITE_TRANSIENT);
        sqlite3_bind_int(st, 2, p->limit);
        sqlite3_bind_int(st, 3, (p->page - 1) * p->limit);

        int ec;
        while ((ec = sqlite3_step(st)) == SQLITE_ROW) {
            a[count] = sqlite3_column_int(st, 0);
            count++;
        }
        if (ec != SQLITE_DONE) {
            fprintf(stderr, "ERR! Failed to get count of matching entries\n");

            goto cleanup;
        }

        sqlite3_finalize(st);
        st = NULL;
    }

cleanup:
    if (st != NULL) {
        sqlite3_finalize(st);
    }

    return count;
}

int jmdict_search_definition(jdic_t *p, const char *query, int *a)
{
    int ret = 0;
    struct sqlite3_stmt *st = NULL;

    sqlite3_finalize(st);
    return ret;
}

