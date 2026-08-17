#pragma once

#include <cstddef>

#ifdef __cplusplus
extern "C" {
#endif

void* extmem_malloc(std::size_t size);
void extmem_free(void* ptr);

#ifdef __cplusplus
}
#endif
