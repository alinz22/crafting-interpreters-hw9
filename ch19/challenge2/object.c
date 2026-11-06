#include <stdio.h>
#include <string.h>

#include "memory.h"
#include "object.h"
#include "value.h"
#include "vm.h"

static Obj* allocateObject(size_t size, ObjType type) {
  Obj* object = (Obj*)reallocate(NULL, 0, size);
  object->type = type;

  object->next = vm.objects;
  vm.objects = object;
  return object;
}

static ObjString* allocateString(const char* chars, int length, bool ownsChars) {
  ObjString* string = (ObjString*)allocateObject(sizeof(ObjString), OBJ_STRING);
  string->ownsChars = ownsChars;
  string->length = length;
  string->chars = chars;
  return string;
}

ObjString* constantString(const char* chars, int length) {
  return allocateString(chars, length, false);
}

ObjString* takeString(char* chars, int length) {
  return allocateString(chars, length, true);
}

void printObject(Value value) {
  switch (OBJ_TYPE(value)) {
    case OBJ_STRING:
      // A constant string has no terminator, so the length has to be passed.
      printf("%.*s", AS_STRING(value)->length, AS_CSTRING(value));
      break;
  }
}
