#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "object.h"
#include "table.h"
#include "vm.h"

static int failures = 0;

#define CHECK(condition) \
    do { \
      if (!(condition)) { \
        printf("FAILED line %d: %s\n", __LINE__, #condition); \
        failures++; \
      } \
    } while (false)

static ObjString* str(const char* text) {
  return copyString(text, (int)strlen(text));
}

static bool getsNumber(Table* table, Value key, double expected) {
  Value value;
  return tableGet(table, key, &value) && IS_NUMBER(value) &&
      AS_NUMBER(value) == expected;
}

static void testEveryKeyType(void) {
  Table table;
  initTable(&table);

  CHECK(tableSet(&table, NUMBER_VAL(1), NUMBER_VAL(10)));
  CHECK(tableSet(&table, BOOL_VAL(true), NUMBER_VAL(20)));
  CHECK(tableSet(&table, BOOL_VAL(false), NUMBER_VAL(30)));
  CHECK(tableSet(&table, NIL_VAL, NUMBER_VAL(40)));
  CHECK(tableSet(&table, OBJ_VAL(str("1")), NUMBER_VAL(50)));

  CHECK(getsNumber(&table, NUMBER_VAL(1), 10));
  CHECK(getsNumber(&table, BOOL_VAL(true), 20));
  CHECK(getsNumber(&table, BOOL_VAL(false), 30));
  CHECK(getsNumber(&table, NIL_VAL, 40));
  CHECK(getsNumber(&table, OBJ_VAL(str("1")), 50));

  // Writing an existing key replaces the value without adding an entry.
  CHECK(!tableSet(&table, NIL_VAL, NUMBER_VAL(41)));
  CHECK(getsNumber(&table, NIL_VAL, 41));

  Value value;
  CHECK(!tableGet(&table, NUMBER_VAL(2), &value));

  freeTable(&table);
}

static void testNumberEdgeCases(void) {
  Table table;
  initTable(&table);

  // 0 and -0 compare equal, so they have to be the same key.
  tableSet(&table, NUMBER_VAL(0.0), NUMBER_VAL(1));
  CHECK(getsNumber(&table, NUMBER_VAL(-0.0), 1));
  CHECK(!tableSet(&table, NUMBER_VAL(-0.0), NUMBER_VAL(2)));
  CHECK(getsNumber(&table, NUMBER_VAL(0.0), 2));

  // NaN never equals itself, so storing it would only leak a bucket.
  int count = table.count;
  CHECK(!tableSet(&table, NUMBER_VAL(NAN), NUMBER_VAL(3)));
  CHECK(table.count == count);
  Value value;
  CHECK(!tableGet(&table, NUMBER_VAL(NAN), &value));

  tableSet(&table, NUMBER_VAL(INFINITY), NUMBER_VAL(4));
  tableSet(&table, NUMBER_VAL(-INFINITY), NUMBER_VAL(5));
  tableSet(&table, NUMBER_VAL(1.5), NUMBER_VAL(6));
  CHECK(getsNumber(&table, NUMBER_VAL(INFINITY), 4));
  CHECK(getsNumber(&table, NUMBER_VAL(-INFINITY), 5));
  CHECK(getsNumber(&table, NUMBER_VAL(1.5), 6));

  freeTable(&table);
}

static void testResizeKeepsEverything(void) {
  Table table;
  initTable(&table);

  for (int i = 0; i < 100000; i++) {
    CHECK(tableSet(&table, NUMBER_VAL(i), NUMBER_VAL(i * 2)));
  }
  CHECK(table.count == 100000);
  for (int i = 0; i < 100000; i++) {
    CHECK(getsNumber(&table, NUMBER_VAL(i), i * 2));
  }

  freeTable(&table);
}

static void testTombstones(void) {
  Table table;
  initTable(&table);

  for (int i = 0; i < 1000; i++) {
    tableSet(&table, NUMBER_VAL(i), NUMBER_VAL(i));
  }

  // Lookups have to keep walking past anything that got deleted.
  for (int i = 0; i < 1000; i += 2) {
    CHECK(tableDelete(&table, NUMBER_VAL(i)));
  }
  for (int i = 0; i < 1000; i++) {
    Value value;
    bool found = tableGet(&table, NUMBER_VAL(i), &value);
    CHECK(found == (i % 2 == 1));
  }
  CHECK(!tableDelete(&table, NUMBER_VAL(0)));

  // nil is a real key here, so deleting it must not look like an empty bucket.
  tableSet(&table, NIL_VAL, NUMBER_VAL(1));
  CHECK(tableDelete(&table, NIL_VAL));
  Value value;
  CHECK(!tableGet(&table, NIL_VAL, &value));

  // Putting a key back into its own tombstone does not count as a new bucket.
  int count = table.count;
  CHECK(tableSet(&table, NIL_VAL, NUMBER_VAL(2)));
  CHECK(table.count == count);
  CHECK(getsNumber(&table, NIL_VAL, 2));

  freeTable(&table);
}

static void testNilValuesAreNotEmptyBuckets(void) {
  Table table;
  initTable(&table);

  tableSet(&table, NUMBER_VAL(1), NIL_VAL);
  tableSet(&table, NIL_VAL, NIL_VAL);

  Value value = NUMBER_VAL(99);
  CHECK(tableGet(&table, NUMBER_VAL(1), &value) && IS_NIL(value));
  value = NUMBER_VAL(99);
  CHECK(tableGet(&table, NIL_VAL, &value) && IS_NIL(value));

  freeTable(&table);
}

static void testAddAll(void) {
  Table from;
  Table to;
  initTable(&from);
  initTable(&to);

  tableSet(&from, NUMBER_VAL(1), NUMBER_VAL(1));
  tableSet(&from, NIL_VAL, NUMBER_VAL(2));
  tableSet(&from, OBJ_VAL(str("key")), NUMBER_VAL(3));
  tableSet(&from, BOOL_VAL(true), NUMBER_VAL(4));
  tableDelete(&from, BOOL_VAL(true));

  tableAddAll(&from, &to);
  CHECK(getsNumber(&to, NUMBER_VAL(1), 1));
  CHECK(getsNumber(&to, NIL_VAL, 2));
  CHECK(getsNumber(&to, OBJ_VAL(str("key")), 3));
  Value value;
  CHECK(!tableGet(&to, BOOL_VAL(true), &value));

  freeTable(&from);
  freeTable(&to);
}

static void testInterning(void) {
  CHECK(str("same") == str("same"));
  CHECK(str("same") != str("different"));

  char* owned = (char*)malloc(5);
  memcpy(owned, "same", 5);
  CHECK(takeString(owned, 4) == str("same"));
}

static void testFindStringSkipsOtherKeys(void) {
  Table table;
  initTable(&table);

  for (int i = 0; i < 100; i++) {
    tableSet(&table, NUMBER_VAL(i), NIL_VAL);
  }
  tableSet(&table, NIL_VAL, NIL_VAL);
  ObjString* key = str("needle");
  tableSet(&table, OBJ_VAL(key), NIL_VAL);

  CHECK(tableFindString(&table, "needle", 6, key->hash) == key);
  CHECK(tableFindString(&table, "nope", 4, 12345) == NULL);

  freeTable(&table);
}

int main(void) {
  initVM();

  testEveryKeyType();
  testNumberEdgeCases();
  testResizeKeepsEverything();
  testTombstones();
  testNilValuesAreNotEmptyBuckets();
  testAddAll();
  testInterning();
  testFindStringSkipsOtherKeys();

  freeVM();

  if (failures > 0) {
    printf("%d checks failed\n", failures);
    return 1;
  }
  printf("all table checks passed\n");
  return 0;
}
