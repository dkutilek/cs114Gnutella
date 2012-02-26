CXXFLAGS =	-O2 -g -Wall -fmessage-length=0

OBJS =		gnutella.o

LIBS =

TARGET =	gnutella

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
