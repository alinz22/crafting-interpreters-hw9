#ifndef clox_vm_h
#define clox_vm_h

#include "chunk.h"
#include "table.h"
#include "value.h"

#define STACK_INITIAL 256
#define MAX_GLOBALS (UINT16_MAX + 1)

typedef struct {
  Chunk* chunk;
  uint8_t* ip;
  Value* stack;
  int stackCapacity;
  Value* stackTop;
  // Name to slot, used only by the compiler.
  Table globalSlots;
  // Indexed by slot. A slot stays UNDEFINED_VAL until its var statement runs.
  ValueArray globalValues;
  // Slot to name, used only to report errors.
  ValueArray globalNames;
  Table strings;
  Obj* objects;
} VM;

typedef enum {
  INTERPRET_OK,
  INTERPRET_COMPILE_ERROR,
  INTERPRET_RUNTIME_ERROR
} InterpretResult;

extern VM vm;

void initVM(void);
void freeVM(void);
InterpretResult interpret(const char* source);
void push(Value value);
Value pop(void);

#endif
