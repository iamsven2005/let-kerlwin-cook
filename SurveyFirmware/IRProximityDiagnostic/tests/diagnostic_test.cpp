#include <cassert>
#include "../diagnostic.h"

int main() {
  using namespace ir_diagnostic;
  unsigned char misses[] = {2, 5, 5, 5, 5, 5, 5};
  unsigned char mixed[] = {3, 5, 2, 5, 5, 5, 5};
  unsigned char all[] = {5, 5, 5, 5, 5, 5, 5};
  assert(consecutiveLevel(misses) == 0);
  assert(consecutiveLevel(mixed) == 2);
  assert(consecutiveLevel(all) == 7);
  assert(hitCount(misses) == 6);
  assert(hitCount(mixed) == 6);
  assert(hitCount(all) == 7);
}
