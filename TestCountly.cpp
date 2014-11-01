#include "Countly.h"
#include <unistd.h>
using namespace CountlyCpp;

int main(int argc, char * argv[])
{
  Countly * ct = Countly::GetInstance();
  
  ct->SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Free", "1.0");
  
  
  ct->Start("abf2034f975393fa994d1cf8adf9a93e4a29ac29", "http://server.com", 8080);
  
  ct->RecordEvent("testk1", 123);
  ct->RecordEvent("testk1", 17);
  ct->RecordEvent("testk1", 34);
  ct->RecordEvent("testk2", 644, 13.3);
 
#ifndef WIN32
  sleep(4);
#else
  Sleep(4000);
#endif
  return 0;
}
