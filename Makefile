CFLAGS = -Wall -O2 -static

all = client forwarder
all: $(all)

clean:
	rm -f $(all)
