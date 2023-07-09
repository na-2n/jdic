-- KANJI

CREATE TABLE jmdict_kanji (
    id          INTEGER PRIMARY KEY,
    seqnum      INTEGER NOT NULL,
    text        TINYTEXT NOT NULL
);

CREATE TABLE jmdict_kanji_tag (
    id          INTEGER PRIMARY KEY,
    kanji       INTEGER NOT NULL,
    text        TINYTEXT NOT NULL,
    FOREIGN KEY(kanji) REFERENCES jmdict_kanji(id)
);

-- READING

CREATE TABLE jmdict_reading (
    id          INTEGER PRIMARY KEY,
    seqnum      INTEGER NOT NULL,
    text        TINYTEXT NOT NULL,
    truereading BOOLEAN NOT NULL DEFAULT TRUE
);

CREATE TABLE jmdict_reading_tag (
    id          INTEGER PRIMARY KEY,
    reading     INTEGER NOT NULL,
    text        TINYTEXT NOT NULL,
    FOREIGN KEY(reading) REFERENCES jmdict_reading(id)
);

CREATE TABLE jmdict_reading_for (
    id          INTEGER PRIMARY KEY,
    reading     INTEGER NOT NULL,
    kanji       INTEGER NOT NULL,
    FOREIGN KEY(reading) REFERENCES jmdict_reading(id),
    FOREIGN KEY(kanji)   REFERENCES jmdict_kanji(id)
);

-- SENSE

CREATE TABLE jmdict_sense (
    id          INTEGER PRIMARY KEY,
    seqnum      INTEGER NOT NULL
);

CREATE TABLE jmdict_sense_gloss (
    id          INTEGER PRIMARY KEY,
    sense       INTEGER NOT NULL,
    lang        TINYTEXT NOT NULL,
    -- TODO might change the following two to enums
    gender      TINYTEXT,
    type        TINYTEXT,
    text        TEXT NOT NULL,
    FOREIGN KEY(sense) REFERENCES jmdict_sense(id)
);

-- TODO implement the following
--CREATE TABLE jmdict_sense_example (
--    id          INTEGER PRIMARY KEY,
--    sense       INTEGER NOT NULL,
--    source      TINYTEXT NOT NULL,
--    form        TINYTEXT NOT NULL,
--    FOREIGN KEY(sense) REFERENCES jmdict_sense(id)
--);

--CREATE TABLE jmdict_sense_example_txt (
--    id          INTEGER PRIMARY KEY,
--    example     INTEGER NOT NULL,
--    lang        TINYTEXT NOT NULL,
--    text        TEXT NOT NULL,
--    FOREIGN KEY(example) REFERENCES jmdict_sense_example(id)
--);

