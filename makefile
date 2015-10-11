CC=gcc
CFLAGS=-Wall -Wextra -g -pedantic 
TARGET=line
.PHONY: all clean
all: $(TARGET) libline.so
doc: $(TARGET).htm 
lib$(TARGET).a: lib$(TARGET).o
	ar rcs $@ $<
lib$(TARGET).so: lib$(TARGET).c lib$(TARGET).h
	$(CC) $(CFLAGS) $< -c -fPIC -o $@
	$(CC) -shared -fPIC $< -o $@
lib$(TARGET).o: lib$(TARGET).c lib$(TARGET).h
	$(CC) $(CFLAGS) $< -c -o $@
$(TARGET): main.c lib$(TARGET).a
	$(CC) $(CFLAGS) $^ -o $@
$(TARGET).htm: $(TARGET).md
	markdown $^ > $@
run: $(TARGET)
	./$^
clean:
	rm -rf $(TARGET) *.a *.so *.o *.log *.htm doxygen
