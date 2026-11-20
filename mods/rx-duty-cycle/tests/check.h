#pragma once

#include <stdio.h>
#include <string.h>

static int failures = 0;

#define CHECK(cond) do { if (!(cond)) { printf("FAIL %s:%d %s\n", __FILE__, __LINE__, #cond); failures++; } } while (0)
#define CHECK_STR(got, want) do { if (strcmp((got), (want)) != 0) { printf("FAIL %s:%d got '%s' want '%s'\n", __FILE__, __LINE__, (got), (want)); failures++; } } while (0)
#define DONE() do { if (failures) printf("%d failure(s)\n", failures); return failures ? 1 : 0; } while (0)
