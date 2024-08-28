#
# NMake script to build TimeLapse.exe, a time lapse camera capture
# utility for Windows.
#
# Tools:  Microsoft Visual Studio 2022
#

.SUFFIXES:  .cpp

.cpp.obj:
    cl -c -W3 -MDd -Od -Zi -D_DEBUG -EHsc $*.cpp

all:    TimeLapse.exe


TimeLapse.exe: TimeLapse.obj CameraFrameGrabber.obj
    link /DEBUG /OUT:$@ $**

TimeLapse.obj:  TimeLapse.cpp CameraFrameGrabber.h

CameraFrameGrabber.obj:  CameraFrameGrabber.cpp CameraFrameGrabber.h

clean:
    if exist *.obj del *.obj
    if exist *.exe del *.exe
    if exist *.ilk del *.ilk
    if exist *.pdb del *.pdb
    if exist *.bak del *.bak
    if exist frame*.bmp del frame*.bmp

