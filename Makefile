BOOT_DIR := boot

.PHONY: all boot clean re run run_clean intokey all_no_clear clean_no_clear

all:
	$(MAKE) -C $(BOOT_DIR) all

boot:
	$(MAKE) -C $(BOOT_DIR) all

clean:
	$(MAKE) -C $(BOOT_DIR) clean

re:
	$(MAKE) -C $(BOOT_DIR) re

run:
	$(MAKE) -C $(BOOT_DIR) run

run_clean:
	$(MAKE) -C $(BOOT_DIR) run_clean

intokey:
	$(MAKE) -C $(BOOT_DIR) intokey

all_no_clear:
	$(MAKE) -C $(BOOT_DIR) all_no_clear

clean_no_clear:
	$(MAKE) -C $(BOOT_DIR) clean_no_clear