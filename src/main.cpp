#include "charybdis/version.hpp"  // NOLINT(misc-include-cleaner)

#include <iostream>
#include <string_view>

int main() {
  std::cout << charybdis::kProjectName << " v" << charybdis::kVersion << "\n";
  return 0;
}
