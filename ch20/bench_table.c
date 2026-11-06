#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "object.h"
#include "table.h"
#include "vm.h"

#define RUNS 5

#ifndef TABLE_MAX_LOAD
#define TABLE_MAX_LOAD 0.75
#endif

// Stops the optimizer from throwing away lookups.
static volatile double sink;

static double now(void) {
  struct timespec time;
  clock_gettime(CLOCK_MONOTONIC, &time);
  return (double)time.tv_sec + (double)time.tv_nsec / 1e9;
}

static void report(const char* name, double seconds, int ops) {
  printf("%-34s %9.1f ns/op\n", name, seconds * 1e9 / ops);
}

// ---------------------------------------------------------------- key sets

static Value* numberKeys(int count, double step) {
  Value* keys = (Value*)malloc(sizeof(Value) * count);
  for (int i = 0; i < count; i++) keys[i] = NUMBER_VAL(i * step);
  return keys;
}

static Value* stringKeys(const char* prefix, int count) {
  Value* keys = (Value*)malloc(sizeof(Value) * count);
  char buffer[512];
  for (int i = 0; i < count; i++) {
    int length = snprintf(buffer, sizeof(buffer), "%s%d", prefix, i);
    keys[i] = OBJ_VAL(copyString(buffer, length));
  }
  return keys;
}

// Keeps only strings whose hash has the same low 12 bits, so every one of them
// wants the same bucket until the table grows past 4096.
static Value* collidingKeys(int count) {
  Value* keys = (Value*)malloc(sizeof(Value) * count);
  char buffer[32];
  int found = 0;
  for (int i = 0; found < count; i++) {
    int length = snprintf(buffer, sizeof(buffer), "c%d", i);
    ObjString* string = copyString(buffer, length);
    if ((string->hash & 0xfff) == 0) keys[found++] = OBJ_VAL(string);
  }
  return keys;
}

// ------------------------------------------------------------- measurements

static double timeInsert(Value* keys, int count) {
  double best = 1e9;
  for (int run = 0; run < RUNS; run++) {
    Table table;
    initTable(&table);
    double start = now();
    for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);
    double elapsed = now() - start;
    if (elapsed < best) best = elapsed;
    freeTable(&table);
  }
  return best;
}

static double timeLookup(Table* table, Value* keys, int count, int ops) {
  double best = 1e9;
  for (int run = 0; run < RUNS; run++) {
    double start = now();
    double found = 0;
    Value value;
    for (int i = 0; i < ops; i++) {
      if (tableGet(table, keys[i % count], &value)) found++;
    }
    double elapsed = now() - start;
    sink = found;
    if (elapsed < best) best = elapsed;
  }
  return best;
}

static void benchKeys(const char* name, Value* keys, int count, Value* misses) {
  char label[64];

  snprintf(label, sizeof(label), "%s insert", name);
  report(label, timeInsert(keys, count), count);

  Table table;
  initTable(&table);
  for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);

  snprintf(label, sizeof(label), "%s hit", name);
  // Small tables go around more than once so the timer has something to measure.
  int ops = count < 500000 ? 500000 : count;
  report(label, timeLookup(&table, keys, count, ops), ops);

  if (misses != NULL) {
    snprintf(label, sizeof(label), "%s miss", name);
    report(label, timeLookup(&table, misses, count, ops), ops);
  }

  freeTable(&table);
}

// The same number of lookups against tables of very different sizes.
static void benchSizes(void) {
  int sizes[] = {8, 1024, 1048576};
  int ops = 2000000;
  Value* keys = numberKeys(sizes[2], 1);

  for (int s = 0; s < 3; s++) {
    Table table;
    initTable(&table);
    for (int i = 0; i < sizes[s]; i++) tableSet(&table, keys[i], NIL_VAL);

    char label[64];
    snprintf(label, sizeof(label), "lookup, table of %d", sizes[s]);
    report(label, timeLookup(&table, keys, sizes[s], ops), ops);
    freeTable(&table);
  }

  free(keys);
}

// Scrambles the order so the lookups below jump around memory.
static void shuffle(Value* keys, int count) {
  srand(42);
  for (int i = count - 1; i > 0; i--) {
    int j = (int)(((double)rand() / ((double)RAND_MAX + 1)) * (i + 1));
    Value temp = keys[i];
    keys[i] = keys[j];
    keys[j] = temp;
  }
}

static void benchRandomOrder(void) {
  int count = 1048576;
  Value* keys = numberKeys(count, 1);
  Table table;
  initTable(&table);
  for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);
  shuffle(keys, count);
  report("lookup, table of 1048576, random", timeLookup(&table, keys, count, count), count);
  freeTable(&table);
  free(keys);
}

// Keeps a small number of live keys while always adding new ones and removing
// old ones, like a queue or a cache. Each delete leaves a tombstone.
static void benchChurn(int live, int ops) {
  Table table;
  initTable(&table);

  double start = now();
  for (int i = 0; i < ops; i++) {
    tableSet(&table, NUMBER_VAL(i), NIL_VAL);
    if (i >= live) tableDelete(&table, NUMBER_VAL(i - live));
  }
  double elapsed = now() - start;

  char label[64];
  snprintf(label, sizeof(label), "churn, %d live keys", live);
  report(label, elapsed, ops);

  int tombstones = 0;
  int entries = 0;
  for (int i = 0; i < table.capacity; i++) {
    Entry* entry = &table.entries[i];
    if (!IS_EMPTY(entry->key)) entries++;
    else if (!IS_NIL(entry->value)) tombstones++;
  }
  printf("  after %d ops: capacity %d, %d entries, %d tombstones\n",
         ops, table.capacity, entries, tombstones);
  freeTable(&table);
}

// Fills a table, deletes all of it, then looks for keys that are not there.
static void benchLookupAfterDelete(int count) {
  Value* keys = numberKeys(count, 1);
  Value* misses = numberKeys(count, 0.5);
  for (int i = 0; i < count; i++) misses[i] = NUMBER_VAL(i + count + 0.5);

  Table table;
  initTable(&table);
  for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);
  report("miss, table full of entries", timeLookup(&table, misses, count, count), count);

  for (int i = 0; i < count; i++) tableDelete(&table, keys[i]);
  report("miss, same table all tombstones", timeLookup(&table, misses, count, count), count);

  freeTable(&table);
  free(keys);
  free(misses);
}

// Fills a table of 2^20 buckets right up to the maximum load. Without this the
// doubling hides the setting, since different maximums can end up at the same
// capacity for the same number of keys.
static void benchFullTable(void) {
  int capacity = 1048576;
  int count = (int)(capacity * TABLE_MAX_LOAD);
  Value* keys = numberKeys(count, 1);
  Value* misses = numberKeys(count, 1);
  for (int i = 0; i < count; i++) misses[i] = NUMBER_VAL(i + count);

  Table table;
  initTable(&table);
  for (int i = 0; i < count; i++) tableSet(&table, keys[i], NIL_VAL);

  printf("full table: %d keys, capacity %d\n", count, table.capacity);
  report("full table hit", timeLookup(&table, keys, count, count), count);
  report("full table miss", timeLookup(&table, misses, count, count), count);

  freeTable(&table);
  free(keys);
  free(misses);
}

int main(int argc, const char* argv[]) {
  bool quick = argc > 1 && strcmp(argv[1], "--numbers") == 0;

  initVM();

  // All of these fit in a table of 1024 buckets.
  int smallCount = 700;
  Value* smallInts = numberKeys(smallCount, 1);
  Value* smallMisses = numberKeys(smallCount, 1);
  for (int i = 0; i < smallCount; i++) smallMisses[i] = NUMBER_VAL(i + smallCount);
  benchKeys("small integer keys", smallInts, smallCount, smallMisses);

  int numbers = 500000;
  Value* ints = numberKeys(numbers, 1);
  Value* intMisses = numberKeys(numbers, 1);
  for (int i = 0; i < numbers; i++) intMisses[i] = NUMBER_VAL(i + numbers);
  benchKeys("integer keys", ints, numbers, intMisses);

  if (quick) {
    freeVM();
    return 0;
  }

  Value* fractions = numberKeys(numbers, 0.37);
  benchKeys("fractional keys", fractions, numbers, NULL);

  int strings = 200000;
  Value* shortKeys = stringKeys("k", strings);
  Value* missKeys = stringKeys("m", strings);
  benchKeys("short strings", shortKeys, strings, missKeys);

  char prefix[257];
  memset(prefix, 'x', 256);
  prefix[256] = '\0';
  double start = now();
  Value* longKeys = stringKeys(prefix, strings);
  report("long strings, interning", now() - start, strings);
  benchKeys("long strings (256 char prefix)", longKeys, strings, NULL);

  int colliding = 2000;
  Value* colliders = collidingKeys(colliding);
  benchKeys("colliding strings", colliders, colliding, NULL);
  Value* regular = stringKeys("regular", colliding);
  benchKeys("regular strings, same count", regular, colliding, NULL);

  benchSizes();
  benchRandomOrder();
  benchChurn(1000, 2000000);
  benchLookupAfterDelete(100000);
  benchFullTable();

  freeVM();
  return 0;
}
