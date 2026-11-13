# ProgramState fixtures

Real snapshots taken with the CodeLens debugger (`./build/make_fixtures tests/fixtures`).
Load one with `state_from_json` to build the memory visualizer without a live session.
Addresses are hex strings. `stack[0]` is `main`.

## swap.json

Inside swap(): a and b point at main's x and y.

```c
#include <stdio.h>
void swap(int *a, int *b) {
    int t = *a;
    *a = *b;
    *b = t;
}
int main(void) {
    int x = 3, y = 5;
    swap(&x, &y);
    printf("%d %d\n", x, y);
    return 0;
}
```

## array.json

An int array and a pointer to one of its elements.

```c
int main(void) {
    int a[5] = {10, 20, 30, 40, 50};
    int *p = &a[2];
    *p = 99;
    return 0;
}
```

## struct.json

A struct with a char array field, a pointer to it, and a string.

```c
#include <string.h>
struct student { int id; double grade; char name[8]; };
int main(void) {
    struct student s = {42, 3.5, "Ann"};
    struct student *ps = &s;
    char word[6] = "hello";
    ps->grade = 3.9;
    return 0;
}
```

## heap.json

A live heap block (int[3]) and a freed block with a dangling pointer.

```c
#include <stdlib.h>
int main(void) {
    int *nums = malloc(3 * sizeof(int));
    nums[0] = 1; nums[1] = 2; nums[2] = 3;
    int *old = malloc(4 * sizeof(int));
    free(old);
    free(nums);
    return 0;
}
```

## leak.json

The program finished but one heap block was never freed.

```c
#include <stdlib.h>
int main(void) {
    int *p = malloc(10 * sizeof(int));
    p = NULL;
    return 0;
}
```

## recursion.json

fact(4) at its deepest call: five frames on the stack.

```c
#include <stdio.h>
int fact(int n) {
    if (n <= 1) return 1;
    return n * fact(n - 1);
}
int main(void) {
    printf("%d\n", fact(4));
    return 0;
}
```

## null_crash.json

Writing through a NULL pointer: segmentation fault on line 4.

```c
#include <stddef.h>
int main(void) {
    int *p = NULL;
    *p = 5;
    return 0;
}
```

