TARGET_SYSTEM := $(subst -, ,$(shell $(CC) -dumpmachine))

CC?=gcc
CFLAGS?=-Wall -Wextra -g -pedantic 

ifeq (mingw32, $(TARGET_SYSTEM))
DLL=dll
RM=del
RM_FLAGS= /Q 
else
DLL=so
ADDITIONAL=
RM=rm 
RM_FLAGS= -rf
endif


TARGET=line
.PHONY: all clean
all: $(TARGET) libline.a
doc: $(TARGET).htm 
lib$(TARGET).a: lib$(TARGET).o
	ar rcs $@ $<
lib$(TARGET).$(DLL): lib$(TARGET).c lib$(TARGET).o lib$(TARGET).h
	$(CC) -shared -fPIC $< -o $@
lib$(TARGET).o: lib$(TARGET).c lib$(TARGET).h
	$(CC) $(CFLAGS) $< -fPIC -c -o $@
$(TARGET): main.c lib$(TARGET).$(DLL)
	$(CC) $(CFLAGS) $^ -o $@
$(TARGET).htm: $(TARGET).md
	markdown $^ > $@
run: $(TARGET)
	./$^
clean:
	$(RM) $(RM_FLAGS) $(TARGET) *.a *.$(DLL) *.o *.log *.htm doxygen
	$(RM) $(RM_FLAGS) $(TARGET) *.$(DLL) 
	$(RM) $(RM_FLAGS) $(TARGET) *.o
	$(RM) $(RM_FLAGS) $(TARGET) *.log
	$(RM) $(RM_FLAGS) $(TARGET) *.htm
	$(RM) $(RM_FLAGS) $(TARGET) doxygen

