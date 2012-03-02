CXXFLAGS =	-O0 -g -Wall -fmessage-length=0

OBJS =		gnutella.o descriptor_header.o payload.o

LIBS =

TARGET =	gnutella

$(TARGET):	$(OBJS)
	$(CXX) -o $(TARGET) $(OBJS) $(LIBS)

all:	$(TARGET)

clean:
	rm -f $(OBJS) $(TARGET)
