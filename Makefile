obj-m := tf_luna.o

tf_luna-y := tf_luna-core.o
tf_luna-y += tf_luna-serdev.o

KDIR := /lib/modules/$(shell uname -r)/build/


all:
	$(MAKE) -C $(KDIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
