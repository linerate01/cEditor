# \ucef4\ud30c\uc77c\ub7ec \uc124\uc815
CC = gcc
CFLAGS = -Wall -g
LDFLAGS = -lncurses

# \uc2e4\ud589 \ud30c\uc77c \uc774\ub984
TARGET = editor

# \uae30\ubcf8 \ud0c0\uac9f
all: $(TARGET)

# \uc2e4\ud589 \ud30c\uc77c \uc0dd\uc131 \uaddc\uce59
$(TARGET): main.c
        $(CC) $(CFLAGS) -o $(TARGET) main.c $(LDFLAGS)

# \uc815\ub9ac
clean:
        rm -f $(TARGET)                       
