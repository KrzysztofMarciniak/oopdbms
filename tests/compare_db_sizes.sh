#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
oopdbms=${OOPDBMS_BIN:-"$root/build/oopdbms"}

if ! command -v sqlite3 >/dev/null 2>&1; then
        echo "sqlite3 is required" >&2
        exit 1
fi
if [ ! -x "$oopdbms" ]; then
        echo "OOPDBMS binary not found or not executable: $oopdbms" >&2
        echo "Build it first with: make build" >&2
        exit 1
fi

workdir=$(mktemp -d)
trap 'rm -rf "$workdir"' EXIT HUP INT TERM
sqlite_db=$workdir/sqlite.db
oopdb_db=$workdir/oopdb.oopdb
record_count=1000

{
        echo 'CREATE TABLE Person (name TEXT, age INTEGER);'
        echo 'BEGIN;'
        i=1
        while [ "$i" -le "$record_count" ]; do
                printf "INSERT INTO Person VALUES ('Person %s', %s);\n" \
                        "$i" "$((20 + i))"
                i=$((i + 1))
        done
        echo 'COMMIT;'
} | sqlite3 "$sqlite_db"

OOPDB=$oopdb_db "$oopdbms" 'CREATE CLASS Person (name text, age integer)'
i=1
while [ "$i" -le "$record_count" ]; do
        OOPDB=$oopdb_db "$oopdbms" \
                "NEW Person (name = \"Person $i\", age = $((20 + i)))" \
                >/dev/null
        i=$((i + 1))
done

OOPDB=$oopdb_db "$oopdbms" 'FIND Person' > "$workdir/oopdb-find.txt"
live_count=$(wc -l < "$workdir/oopdb-find.txt" | tr -d ' ')
sqlite_size=$(wc -c < "$sqlite_db" | tr -d ' ')
oopdb_size=$(wc -c < "$oopdb_db" | tr -d ' ')
format_version=$(od -An -tu1 -j14 -N2 "$oopdb_db" | awk '{print $1 + 256 * $2}')
max_oopdb_size=$((record_count * 40 + 4096))

printf 'Same dataset: %s Person records\n' "$record_count"
printf 'SQLite file:  %s bytes (%s)\n' "$sqlite_size" "$sqlite_db"
printf 'OOPDB file:   %s bytes (%s)\n' "$oopdb_size" "$oopdb_db"
printf 'OOPDB format: version %s\n' "$format_version"
if [ "$format_version" -ne 2 ]; then
        echo "Expected compact OOPDB format version 2, found $format_version" >&2
        exit 1
fi
if [ "$live_count" -ne "$record_count" ]; then
        echo "Expected $record_count live OOPDB records, found $live_count" >&2
        exit 1
fi
if [ "$oopdb_size" -gt "$max_oopdb_size" ]; then
        echo "OOPDB file exceeds the expected log-size bound of $max_oopdb_size bytes" >&2
        exit 1
fi
