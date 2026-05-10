#include "projectcharybdis/version.hpp"  // NOLINT(misc-include-cleaner)

#include <iostream>

int main() {
  std::cout << projectcharybdis::kProjectName << " v" << projectcharybdis::kVersion << "\n";
  return 0;
}
