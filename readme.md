# Coro.h
## Single Header C Coroutine Implementation

### Usage
Just include the header, defining *ONCE* CORO_IMPLEMENTATION before including:
```c
#define CORO_IMPLEMENTATION
#include "coro.h"
```  
"async.h" provides a different implementation that is x86_64 only.

### Example
<details>
<summary>Producer / Consumer</summary>  

```c  
#define CORO_IMPLEMENTATION
#include "async.h"
#include <stdio.h>

void producer(void *arg) {
    (void)arg;
    const char *items[] = { "apple", "banana", "cherry", NULL };
    for (int i = 0; items[i]; i++)
        coro_yield((void *)items[i]);
}

void consumer(void *arg) {
    Coro prod = (Coro)arg;
    char *item;
    while ((item = coro_resume(prod, NULL)) != NULL)
        printf("consumed: %s\n", item);
}

int main(void) {
    Coro prod = coro_create(producer);
    Coro cons = coro_create(consumer);

    coro_resume(cons, prod);  // consumer drives the producer internally

    coro_destroy(cons);
    coro_destroy(prod);
    return 0;
}
```  
</details>
   
### Compatibility
Works with Apple Silicon (M Series CPUs), x86_64, and armv6l (tested on Raspberry Pi Zero)
