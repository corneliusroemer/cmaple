//
//  operatingsystem.cpp
//  alignment
//
//  Created by James Barbetti on 29/7/20.
//

#include "operatingsystem.h"
#include <sstream>
#include <string>
#if defined(WIN32) || defined(WIN64)
#include <io.h>  //for _isatty
#else
#include <unistd.h>  //for isatty
#endif

auto getOSName() -> std::string {
  std::stringstream out;
#if defined _WIN32 || defined WIN32 || defined WIN64
  out << "Windows";
#elif defined __APPLE__ || defined __MACH__
  out << "Mac OS X";
#elif defined __linux__
  out << "Linux";
#elif defined __unix__ || defined __unix
  out << "Unix";
#else
  out << "Unknown Platform";
#endif
  out << " " << 8 * sizeof(void*) << "-bit";
  return out.str();
}

auto isStandardOutputATerminal() -> bool {
#if defined(WIN32) || defined(WIN64)
  return _isatty(fileno(stdout));
#else
  return isatty(fileno(stdout));
#endif
}
