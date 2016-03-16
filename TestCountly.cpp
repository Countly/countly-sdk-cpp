/*
 TestCountly.cpp
 CountlyCpp
 
 Created by Benoit Girard on 26/10/14.
 
 The MIT License (MIT)
 
 Copyright (c) 2015 Kontrol SAS (tanker.io)
 
 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:
 
 The above copyright notice and this permission notice shall be included in all
 copies or substantial portions of the Software.
 
 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 SOFTWARE.
 */

#include "Countly.h"
#include <stdlib.h>

#ifndef _WIN32
#include <unistd.h>
#endif

using namespace CountlyCpp;
using namespace std;

int main(int argc, char * argv[])
{
  Countly * ct;
  
  if (argc < 3)
  {
    cout << "Usage : " << argv[0] << " <host> <port>" << endl;
    cout << "Example : " << argv[0] << " http://myserver.com 8080" << endl;
    return -1;
  }
  ct = Countly::GetInstance();
  ct->SetPath("./");
  ct->SetMetrics("Windows 10", "10.22", "Mac", "800x600", "Free", "1.0");
  
  
  ct->Start("ce894ea797762a11560217117abea9b1e354398c", argv[1], atoi(argv[2]));
  ct->RecordEvent("testk1", 123);
  ct->RecordEvent("testk1", 17);
  ct->RecordEvent("testk1", 34);
  ct->RecordEvent("testk2", 644, 13.3);
 
#ifndef _WIN32
  sleep(4);
#else
  Sleep(4000);
#endif
  
  Countly::DeleteInstance();
  
  return 0;
}
