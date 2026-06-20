SRC := writer.c
TARGET := writer
CC=$(CROSS_COMPILE)gcc

all: $(TARGET)

$(TARGET): $(SRC)
	$(CC) -o $(TARGET) $(SRC)
clean:
	rm -f *.o $(TARGET)
