#include "defines.h"
#include <stdio.h>

// #############################################################################
//                           Platform Globals
// #############################################################################
static bool running = true;


// #############################################################################
//                           Platform Functions
// #############################################################################
bool platform_create_window(int width, int height, char* title);
void platform_update_window();


// #############################################################################
//                           Windows Platform
// #############################################################################
#ifdef _WIN32
#include "win32_platform.cpp"
#else // Linux
#include "linux_platform.cpp"
#endif

int main()
{
  platform_create_window(1200, 720, "Schnitzel Motor");
  
  while(running)
  {
    platform_update_window();
  }
  
  return 0;
}

















